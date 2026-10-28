# Sourced by AppRun before fugue-qt starts.
#
# Chromium (Qt WebEngine) sandboxes its renderers with user namespaces. The setuid chrome-sandbox helper cannot be used from
# an AppImage (its FUSE mount is nosuid), so user namespaces are the only sandbox available here. Some systems refuse them to
# unprivileged programs (Ubuntu 23.10+ with apparmor_restrict_unprivileged_userns, kernels with
# kernel.unprivileged_userns_clone=0, containers). There WebEngine would not start at all, so this turns its sandbox off for
# this run and says so on stderr. Where user namespaces work the sandbox stays on.
#
#   FUGUE_KEEP_SANDBOX=1   never fall back: if user namespaces are refused, the HTML windows fail instead of running unsandboxed
#   QTWEBENGINE_DISABLE_SANDBOX / QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox   already chosen by the caller, left alone
fugue_userns_ok() {
  [ "$(cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>/dev/null)" = 1 ] && return 1
  [ "$(cat /proc/sys/kernel/unprivileged_userns_clone 2>/dev/null)" = 0 ] && return 1
  [ "$(cat /proc/sys/user/max_user_namespaces 2>/dev/null)" = 0 ] && return 1
  command -v unshare >/dev/null 2>&1 || return 0
  unshare --user --pid --fork true >/dev/null 2>&1
}
case " ${QTWEBENGINE_CHROMIUM_FLAGS:-} " in
  *" --no-sandbox "*) ;;
  *)
    if [ -z "${QTWEBENGINE_DISABLE_SANDBOX:-}" ] && [ "${FUGUE_KEEP_SANDBOX:-}" != 1 ] && ! fugue_userns_ok; then
      export QTWEBENGINE_DISABLE_SANDBOX=1
      echo "fugue: this system does not allow the user namespaces Chromium's sandbox needs; the HTML windows (chat, game interfaces) run WITHOUT the web sandbox. Set FUGUE_KEEP_SANDBOX=1 to refuse that." >&2
    fi
    ;;
esac
unset -f fugue_userns_ok
