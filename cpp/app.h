#pragma once
#include "core.h"
namespace lp {
enum Command {
    New = 100,
    Open,
    Save,
    SaveAs,
    SaveAll,
    Close,
    Exit,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    SelectAll,
    Duplicate,
    DeleteLine,
    LineUp,
    LineDown,
    SortAsc,
    SortDesc,
    AutoIndent,
    Find,
    Replace,
    FindNext,
    FindPrev,
    GoTo,
    Source,
    Preview,
    Split,
    Refresh,
    WhiteSpace,
    EolVisible,
    ControlVisible,
    AllSymbols,
    Wrap,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    FoldCurrent,
    FoldAll,
    UnfoldAll,
    AsHex,
    AsText,
    ExternalCheck,
    Monitor,
    FollowTail,
    Column,
    AddNext,
    AddAll,
    Record,
    MacroStop,
    Play,
    Repeat,
    MacroSave,
    MacroManage,
    Format,
    CompareSelect,
    Compare,
    Hash,
    RecoveryFolder,
    About,
    RegisterShell,
    UnregisterShell,
    Scripts,
    CompareNext,
    ComparePrev,
    Help,
    EditorFont,
    RecentClear,
    ReopenClosed,
    CloseOthers,
    CloseRight,
    CloseAll,
    CopyPath,
    UpperCase,
    LowerCase,
    TitleCase,
    ToggleComment,
    BraceJump,
    Settings,
    FindAll,
    MarkAll,
    ClearMarks,
    FindInFiles,
    ResultsPanel,
    ResultNext,
    ResultPrev,
    BookmarkToggle,
    BookmarkNext,
    BookmarkPrev,
    BookmarkClear,
    BookmarkMatches,
    BookmarkCopy,
    BookmarkDelete,
    SplitEditor,
    OutlinePanel,
    FoldLevel = 300,
    UnfoldLevel = 310,
    EncodingReopen = 400,
    EncodingConvert = 500,
    LanguageSelect = 700,
    SavedMacro = 1000,
    RecentFile = 2000
};
inline constexpr size_t recentLimit = 15, closedLimit = 20;
struct ClosedTab {
    fs::path path;
    bool binary = false;
};
struct SearchOptions {
    std::string query, replacement;
    int flags = 0;
    int hexMode = 1;
    bool allTabs = false;
};
Pos findTarget(HWND h, const SearchOptions &options, Pos a, Pos b);
struct SearchHit {
    std::string docId; // Open tab that was searched; empty for find-in-files hits.
    fs::path path;     // File on disk, when there is one.
    std::wstring source;
    Pos line = 0, column = 0, length = 0;
    std::wstring text;
};
inline constexpr size_t hitLimit = 100000;
inline constexpr int markIndicator = 10, bookmarkMarker = 24;
struct OutlineItem {
    std::wstring name;
    Pos line = 0;
    int depth = 0;
};
// Light-weight symbol list (functions, classes, headings) for the outline panel. Empty when the
// language has no rules.
std::vector<OutlineItem> buildOutline(const std::string &text, const std::string &language);
bool outlineSupported(const std::string &language);
struct Document {
    std::string id = guid(), name, language = "normal";
    fs::path path;
    Encoding encoding;
    HWND editor = nullptr, hexWindow = nullptr, previewHost = nullptr;
    // Same-document split: `secondary` shares editor's Scintilla document. Focus swaps the two so
    // `editor` is always the view in use; `topPane` keeps their on-screen order.
    HWND secondary = nullptr, topPane = nullptr;
    std::unique_ptr<BinaryBuffer> binary;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> browser;
    bool loading = false, recovered = false, encodingChanged = false, large = false, monitor = false,
         follow = true, scripts = false, previewPending = false, previewInitializing = false;
    int viewMode = 0;
    uint64_t revision = 0, renderedRevision = ~uint64_t{}, snapshotRevision = ~uint64_t{};
    fs::path snapshot;
    Stamp disk, notified;
    bool notifiedValid = false;
    Pos caret = 0, anchor = 0, firstRow = 0;
    bool insert = false, ascii = false, lowNibble = false;
    bool hexSelecting = false;
    Pos monitorOffset = 0;
    Bytes monitorPending, monitorHead, monitorBoundary;
    std::string monitorStatus;
    bool bigEndian = false;
    std::string origin, previewHtml;
    std::optional<Pos> compareFirst;
    bool dirty() const {
        return recovered || encodingChanged ||
               (binary ? binary->dirty() : editor && sci(editor, SCI_GETMODIFY) != 0);
    }
    ~Document();
};
struct MacroStep {
    int kind = 0;
    int message = 0;
    int64_t w = 0;
    int64_t l = 0;
    std::string data;
    SearchOptions search;
    bool backwards = false, descending = false, number = false;
    int64_t start = 0, increment = 1;
};
void to_json(Json &j, const MacroStep &s);
void from_json(const Json &j, MacroStep &s);
struct SavedMacroEntry {
    std::string id, name;
    std::vector<MacroStep> steps;
};
class App {
  public:
    HINSTANCE instance;
    HWND window = nullptr, tabs = nullptr, toolbar = nullptr, status = nullptr, findWindow = nullptr,
         compareWindow = nullptr;
    HFONT font = nullptr, mono = nullptr;
    HMENU menu = nullptr, viewMenu = nullptr, macroMenu = nullptr, languageMenu = nullptr, recentMenu = nullptr;
    HIMAGELIST toolbarImages = nullptr, tabImages = nullptr;
    ULONG_PTR gdiplusToken = 0;
    fs::path profile;
    Languages languages;
    std::vector<std::shared_ptr<Document>> docs;
    Json unavailable = Json::array();
    int active = -1, untitled = 0;
    int hotTabClose = -1;
    std::string pressedTabClose;
    bool busy = false, starting = true, closing = false, checking = false, syncing = false;
    bool autoIndent = true, whiteSpace = false, eolVisible = false, controlVisible = false, recording = false,
         playing = false, macroSuppressed = false, newlineActive = false, newlineObserved = false;
    std::atomic_bool cancel{false};
    bool maximized = false;
    int dpi = 96;
    std::string editorFont = "Consolas";
    int editorFontSize = 1100; // Hundredths of a point, as used by Scintilla.
    int editorLineSpacing = 0; // Extra logical pixels between text rows, at 96 DPI.
    int tabWidth = 4;
    bool useTabs = false, braceMatch = true;
    std::vector<fs::path> recentFiles;
    std::vector<ClosedTab> closedTabs;
    std::string dragTab; // Id of the document whose tab is being dragged.
    HWND resultsList = nullptr, resultsLabel = nullptr, searchBuffer = nullptr;
    bool resultsVisible = false, searching = false;
    std::vector<SearchHit> hits;
    int hitIndex = -1;
    std::wstring resultsTitle;
    fs::path findFolder;
    std::wstring findFilter = L"*.*";
    bool findSubfolders = true;
    HWND outlineList = nullptr;
    bool outlineVisible = false;
    std::vector<OutlineItem> outlineItems;
    std::string outlineDocId;
    uint64_t outlineRevision = ~uint64_t{};
    std::vector<MacroStep> recorded, lastMacro;
    std::vector<SavedMacroEntry> savedMacros;
    std::set<std::string> compareIds;
    SearchOptions search;
    std::string searchMessage;
    uint64_t backupTick = 0, previewTick = 0;
    std::vector<std::string> initialFiles;
    bool demo = false;
    int testExit = 1;
    App(HINSTANCE instance, fs::path profile) : instance(instance), profile(std::move(profile)) {}
    ~App();
    void create(int show);
    void start();
    void layout();
    void buildMenu();
    void buildToolbar();
    void update();
    static RECT tabCloseBounds(RECT item, int dpi);
    RECT tabCloseRect(int index) const;
    int tabCloseHit(POINT point) const;
    void updateTabCloseHover(POINT point);
    void refreshTabCloseHover();
    void drawTabItem(const DRAWITEMSTRUCT &item);
    bool tabCloseMessage(UINT message, WPARAM w, LPARAM l);
    void command(int id);
    Document *current() const {
        return active >= 0 && active < static_cast<int>(docs.size()) ? docs[active].get() : nullptr;
    }
    std::shared_ptr<Document> newDocument(std::string name = {}, bool binary = false);
    HWND createEditor(Document *d, HWND parent = nullptr);
    void configureEditor(HWND h);
    void applyEditorFont(HWND h);
    void chooseEditorFont();
    void applyEditorSettings(HWND h);
    void settings();
    void addRecent(const fs::path &path);
    void rebuildRecentMenu();
    void openRecent(size_t index);
    void reopenClosed();
    void closeTabs(int mode); // 0: others, 1: right of the active tab, 2: all
    void moveTab(int from, int to);
    void copyPath();
    void changeCase(int message);
    void titleCase();
    void toggleComment();
    void highlightBrace(Document &d);
    void jumpBrace();
    void createResultsPanel();
    void showResults(bool visible);
    void setResults(std::vector<SearchHit> found, std::wstring title);
    size_t findAll();
    size_t markAll();
    void clearMarks();
    void findInFiles();
    size_t findInFolder(const fs::path &folder, const std::wstring &filter, bool subfolders,
                        const SearchOptions &options);
    void goToHit(int index);
    void navigateHits(bool backwards);
    std::vector<HWND> views(const Document &d) const;
    void toggleBookmark();
    void nextBookmark(bool backwards);
    void clearBookmarks();
    size_t bookmarkMatches();
    std::string copyBookmarked();
    size_t deleteBookmarked();
    void toggleSplit();
    void createOutlinePanel();
    void showOutline(bool visible);
    void refreshOutline(bool force);
    void goToOutline(int index);
    void select(int index);
    void open(const fs::path &path, std::optional<bool> binary = {}, std::optional<Encoding> encoding = {});
    void openPaths(const std::vector<std::string> &paths, bool activate = true);
    void chooseOpen();
    bool save(Document &d, bool saveAs = false);
    bool closeTab(int index);
    void exit();
    void reload(Document &d, std::optional<bool> binary = {}, std::optional<Encoding> encoding = {});
    void backup();
    void restore();
    void savePlacement();
    void restorePlacement(int &show);
    void timer();
    void checkExternal();
    void pollMonitors();
    void toggleMonitor();
    void setView(int mode);
    void refreshPreview(Document &d, bool force = false);
    void ensureBrowser(std::shared_ptr<Document> d);
    void renderPreview(Document &d);
    void showFind(bool replace);
    void updateFind();
    bool findNext(bool backwards = false, bool wrap = true);
    Pos countMatches(bool replace = false);
    bool replaceOne();
    Bytes searchBytes() const;
    void searchAction(int action);
    void goTo();
    void column();
    void format();
    void sort(bool descending);
    void edit(int message);
    void fold(int depth, bool expanded);
    void compare();
    void compareNavigate(bool backwards);
    void syncCompare(Document &d);
    void newline(Document &d, UINT msg, WPARAM w, LPARAM l);
    void capture(const SCNotification &n);
    void toggleRecord();
    void stopRecord();
    void play(const std::vector<MacroStep> &steps, int64_t count = 1, bool toEnd = false);
    void runStep(const MacroStep &s);
    void repeat();
    void saveMacro();
    void manageMacros();
    void loadMacros();
    void persistMacros();
    void recordAction(MacroStep s);
    void paintHex(Document &d, HDC dc);
    void hexInput(Document &d, UINT msg, WPARAM w, LPARAM l);
    void hexEnsureVisible(Document &d);
    void hexChanged(Document &d);
    void message(const std::wstring &text, const std::wstring &title = L"LHPad",
                 UINT flags = MB_OK | MB_ICONINFORMATION);
    void about();
    int ask(const std::wstring &text, const std::wstring &title = L"LHPad",
            UINT flags = MB_YESNOCANCEL | MB_ICONQUESTION);
    void guard(const std::function<void()> &f);
    bool shortcut(MSG &m);
    static LRESULT CALLBACK windowProc(HWND h, UINT m, WPARAM w, LPARAM l);
    static LRESULT CALLBACK editorProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK childProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK hexProc(HWND h, UINT m, WPARAM w, LPARAM l);
    static LRESULT CALLBACK findProc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT notify(NMHDR *header);
};
extern App *app;
struct Field {
    std::wstring label, value;
    bool check = false, checked = false;
    int id = 0;
};
std::optional<std::vector<Field>> inputDialog(HWND owner, const std::wstring &title,
                                              std::vector<Field> fields,
                                              const std::wstring &explanation = L"");
int runTests(App &app, bool full = true);
} // namespace lp
