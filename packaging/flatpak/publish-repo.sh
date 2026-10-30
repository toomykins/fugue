#!/usr/bin/env bash
# publish-repo.sh <fugue.flatpak> <site dir> <gpg key id> <site url> [gpg homedir]
#
# Builds the static site GitHub Pages serves: a signed Flatpak repository holding only the given bundle, plus the
# .flatpakref and .flatpakrepo files that install from it. Each release replaces the whole site, so the repository never
# grows; clients update across that because a commit needs no parent in the repository, only a newer timestamp.
set -euo pipefail
bundle="${1:?usage: publish-repo.sh <fugue.flatpak> <site dir> <gpg key id> <site url> [gpg homedir]}"
site="${2:?site dir}"
key="${3:?gpg key id}"
url="${4:?site url}"
url="${url%/}"
gpg_args=(--gpg-sign="$key")
gpg_cmd=(gpg)
if [ -n "${5:-}" ]; then
  gpg_args+=(--gpg-homedir="$5")
  gpg_cmd+=(--homedir "$5")
fi
app=io.github.toomykins.Fugue
branch=master

[ ! -e "$site" ] || { echo "publish-repo: $site already exists" >&2; exit 1; }
mkdir -p "$site"
pubkey="$("${gpg_cmd[@]}" --export "$key" | base64 -w0)"
[ -n "$pubkey" ] || { echo "publish-repo: no public key for $key" >&2; exit 1; }

ostree --repo="$site/repo" init --mode=archive-z2
flatpak build-import-bundle "${gpg_args[@]}" "$site/repo" "$bundle"
flatpak build-update-repo "${gpg_args[@]}" --title=Fugue --default-branch="$branch" --homepage=https://github.com/toomykins/fugue \
  --generate-static-deltas "$site/repo"
rm -f -- "$site/repo/.lock"

cat >"$site/$app.flatpakref" <<EOF
[Flatpak Ref]
Name=$app
Branch=$branch
Title=Fugue
Url=$url/repo
SuggestRemoteName=fugue
RuntimeRepo=https://dl.flathub.org/repo/flathub.flatpakrepo
GPGKey=$pubkey
EOF

cat >"$site/fugue.flatpakrepo" <<EOF
[Flatpak Repo]
Title=Fugue
Url=$url/repo
Homepage=https://github.com/toomykins/fugue
DefaultBranch=$branch
GPGKey=$pubkey
EOF

cat >"$site/index.html" <<EOF
<!doctype html>
<html lang="en">
<meta charset="utf-8">
<title>Fugue Flatpak repository</title>
<h1>Fugue Flatpak repository</h1>
<p>Install Fugue from this repository and <code>flatpak update</code> keeps it current:</p>
<pre>flatpak install --user $url/$app.flatpakref</pre>
<p><a href="https://github.com/toomykins/fugue">Fugue on GitHub</a></p>
</html>
EOF
