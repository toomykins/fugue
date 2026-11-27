// The pager window, laid out like stock BYOND 516's pager: a dark theme, blue pill buttons, two columns.
//   LEFT : the Games list (saved servers, with a lock when this account cannot join), "Filter:" and "Open...".
//   RIGHT: tabs Home | the selected game | Details, and the top-right buttons Open Location, Details and the gear menu.
// The Sign in window is a small modal dialog: Account, Password, Get an account / Forgot password?,
// "Sign in automatically when Fugue starts", Play as guest, Sign in, Cancel. There is no saved password:
// an account with a saved identity signs in with none. The identity is always kept; only Clear cache removes it.
// Mapping: the game list is the saved servers (pager.json); a click opens the game's tab (address, why it cannot be joined,
// Join); Open... and Open Location... add a server by address; Logout signs out and keeps the identity files; Preferences
// holds the account settings, the Details toggle and Clear cache (pager/cache.h). The technical detail (readiness checklist,
// key and identity chips, paths, client log) is on the Details tab.
// The window draws the model (model.h) and owns no logic: blockers and checklists come from model.h, and the plain sentences
// from model.h:plainBlocker and signin.h:plainSignInMessage. It touches no network and holds no password beyond its fields:
// sign-in is a signal, and the caller runs signin.h off the UI thread.
#pragma once
#include <QSize>
#include <QWidget>
#include <functional>
#include <vector>
#include "account/store.h"
#include "pager/model.h"

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QTabWidget;
class QToolButton;

namespace fugue::pager {

class PagerWindow : public QWidget {
    Q_OBJECT
public:
    explicit PagerWindow(QWidget* parent = nullptr);

    // Replace the displayed state. Pure data in, widgets out: nothing is read from disk or the network here
    // (except the resource-cache size for the Setup line and Preferences).
    void setState(const account::Config& cfg, const Status& st);
    // Convenience for the real app: load pager.json + gather status from $HOME.
    void reload(const CheckOptions& opt = {});

    // ---- the account row + the Sign in window ----
    enum Page {
        SignInPage = 0,
        ServersPage = 1
    };  // SignInPage = not signed in (and not a guest): joins wait for the Sign in window
    Page page() const;
    void showSignIn(const QString& message = {});  // opens the Sign in window (window-modal; does not block)
    QDialog* signInDialog() const;
    bool signInVisible() const;
    // While a sign-in runs the window's fields are disabled; on failure the plain message shows in it; on success it closes.
    void setSigningIn(bool busy);
    void setSignInError(const QString& plain);
    void
    closeSignIn();  // the caller's sign-in succeeded (after setState): the window closes, a held account is released
    void setGuestChosen(
        bool
            guest);  // "Play as guest" chosen (this run, or remembered: launch.h guestRemembered); else a signed-out pager opens on Sign in
    // Preferences / Sign in window "Sign in automatically when Fugue starts" (launch.h autoSignIn). The caller passes the stored
    // value in; a click emits autoSignInChanged. With it off, holdSignIn() makes a saved account wait for the player to confirm
    // it in the Sign in window (no password needed when this computer has its saved sign-in).
    void setAutoSignIn(bool on);
    bool autoSignInChecked() const;
    void holdSignIn();

    // ---- the game list (saved servers) ----
    void setFilter(const QString& text);  // the Filter box: case-insensitive substring of name or address
    int visibleGameCount() const;         // rows the filter leaves visible

    // ---- Details (the technical half) ----
    enum Tab { SetupTab = 0, LogTab = 1 };
    void showTab(Tab t);  // opens the Details tab on that page
    void setDetailsVisible(bool on);
    bool detailsVisible() const;

    // ---- the gear menu / Preferences ----
    QMenu* gearMenu() const;
    QAction* menuAction(
        const QString& objectName) const;  // "about", "openloc", "logout", "prefs", "resources", "ontop", "exit"
    void showPreferences();
    QDialog* preferencesDialog() const;  // null until first shown
    // The Clear cache confirmation: by default a QMessageBox; a hook can replace it (returns whether the player said yes).
    void setConfirmHook(std::function<bool(const QString& title, const QString& text)> hook);
    // Preferences > Clear cache: confirm (listing exactly what goes, with sizes), then emit clearCacheRequested.
    void requestClearCache();

    // Introspection hooks (tests and the shot tool).
    QString joinBlockerText() const;  // the selected server's plain blocker, or "" when it can be joined
    bool joinEnabled() const;         // the Join button for the selected server
    int bookmarkCount() const;
    void selectBookmark(int row);
    void joinRow(int row);     // = pressing Join with that row selected (or double-clicking it)
    void warmRow(int row);     // = pressing Download resources with that row selected (Cancel download while one runs)
    bool warmEnabled() const;  // the Download resources / Cancel download button
    QString gameNoteText() const;  // the game tab's note line (warm-up state / progress)
    QString checklistText() const;
    QString statusText() const;
    QString accountText() const;
    QString signInErrorText() const;
    QString gameTabTitle() const;  // the selected game's tab, "" when closed
    // Opens the Sign in window and fills it. An empty password uses this computer's saved sign-in for that account, if any.
    void submitSignIn(const QString& user, const QString& password);
    // Type into "Open..." (add server) and press Add (tests). Returns the plain error shown, "" on success.
    QString submitAddServer(const QString& text);
    void showAddServer();  // = Open... / Open Location...

    // The log pane: the joined client's merged stdout/stderr, streamed by the caller. `running` disables Join while a client is up.
    void appendLog(const QString& text);
    void setClientRunning(bool running, const QString& note = {});
    void setStatus(const QString& plain, bool error = false);
    // A warm-up (Download resources) is running: the button becomes Cancel download (warmCancelRequested), Join stays usable
    // (the caller stops the download before it starts the game), and `progress` shows on the game tab and the status line.
    // setWarming(false, result) ends it; the caller has already stamped lastPrefetch on success (setState).
    void setWarming(bool on, const QString& progress = {}, bool error = false);
    QString logText() const;

    // ---- closing while a game runs (model.h closeAction) ----
    // While a game runs a tray icon offers Open Fugue and Exit; closing the window hides it there (a notice says so once).
    // When the game ends, or the tray goes away while the window is hidden, the window comes back. Exit, from the gear menu
    // or the tray, asks before it ends a running game, then emits quitRequested. A session ending is never refused.
    void requestQuit();
    void showPager();
    bool hiddenToTray() const;
    QSystemTrayIcon* trayIcon() const;                  // null until a game first runs with a tray available
    void setTrayHook(std::function<bool()> available);  // replaces QSystemTrayIcon::isSystemTrayAvailable (tests)
    void setNoticeHook(
        std::function<void(const QString& title, const QString& text)> hook);  // replaces the tray balloon (tests)

signals:
    void joinRequested(const QString& addr, bool hub, const QString& account);
    void warmRequested(const QString& addr, bool hub, const QString& account);  // run the headless prefetch pass for it
    void warmCancelRequested();                                                 // stop it; what arrived is kept
    void signInRequested(const QString& user, const QString& password);         // the caller must not keep the password
    void savedSignInChosen(
        const QString& account);      // Sign in with no password: this computer already holds that account's identity
    void autoSignInChanged(bool on);  // the caller stores it (launch.h setAutoSignIn)
    void guestRequested();
    void signOutRequested();
    void serverAdded(const QString& addr);  // already parsed (parseTarget); the caller remembers + saves
    void serverRemoved(const QString& addr);
    void accountApplied(const QString& account);  // Details > Setup: set an account name without signing in
    void clearCacheRequested();                   // confirmed by the player; the caller runs pager/cache.h clearCache
    void quitRequested();                         // the caller quits (stopping a running game)

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void rebuildServers();
    void rebuildRecent();
    void updateJoin();
    void updateGameTab();
    void updateMenu();
    void updatePrefs();
    void applyFilter();
    void choosePage();
    void openGameTab();
    void buildSignInDialog();
    void submitDialog();
    void updateSignInNote();
    bool hasSavedSignIn(const QString& account) const;
    bool trayAvailable() const;
    void updateTray();
    bool confirmQuit();
    account::Config cfg_;
    Status st_;
    std::vector<account::Server> sorted_;

    // account row
    QStackedWidget* pages_;
    QLabel *who_, *status_, *recent_;
    QPushButton *acctBtn_, *rowSignIn_, *rowGuest_;
    // the Sign in window
    QDialog* signIn_ = nullptr;
    QComboBox* userEdit_ = nullptr;
    QLineEdit* passEdit_ = nullptr;
    QPushButton *signInBtn_ = nullptr, *guestBtn_ = nullptr, *cancelBtn_ = nullptr;
    QCheckBox* autoBox_ = nullptr;
    QLabel *signInMsg_ = nullptr, *signInNote_ = nullptr;
    // left column
    QPushButton* openBtn_;
    QLineEdit* filter_;
    QListWidget* marks_;
    QWidget* addRow_;
    QLineEdit* addEdit_;
    QPushButton *addOk_, *addCancel_;
    QLabel* addMsg_;
    // right pane
    QTabWidget* right_;
    QWidget *home_, *game_;
    QLabel *gameIcon_, *gameName_, *gameAddr_, *gameWhy_, *gameNote_;
    QPushButton *gameJoin_, *gameWarm_, *gameRemove_;
    QToolButton *openTool_, *detailsTool_, *gearTool_;
    QMenu* gear_;
    QDialog* prefs_ = nullptr;
    QCheckBox* prefsDetails_ = nullptr;
    QCheckBox* prefsAuto_ = nullptr;
    QLabel* prefsAccount_ = nullptr;
    QLabel* prefsCache_ = nullptr;
    QPushButton* prefsClear_ = nullptr;
    std::function<bool(const QString&, const QString&)> confirm_;
    // details
    QWidget* details_;
    QLabel *keyChip_, *identChip_, *clientChip_, *joinNote_, *cacheLabel_, *cfgPathLabel_;
    QPushButton* apply_;
    QTabWidget* tabs_;
    QLineEdit* acctEdit_;
    QPlainTextEdit* checklist_;
    QPlainTextEdit* log_;
    bool running_ = false, guest_ = false, busy_ = false, haveState_ = false, warming_ = false;
    QString warmNote_;
    bool autoSignIn_ = true, held_ = false;  // held_: holdSignIn() -- a saved account waits for the Sign in window
    QString runNote_;
    QSystemTrayIcon* tray_ = nullptr;
    QTimer* trayWatch_ = nullptr;
    std::function<bool()> trayHook_;
    std::function<void(const QString&, const QString&)> notice_;
    bool parked_ = false, trayNoticed_ = false, quitting_ = false;  // parked_: closed while a game runs
};

}  // namespace fugue::pager
