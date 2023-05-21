#include <Corrade/Containers/Pair.h>
#include <Corrade/PluginManager/Manager.h>
#include <Corrade/Utility/DebugStl.h>
#include <Corrade/Utility/Path.h>
#include <Corrade/Utility/Resource.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/Version.h>
#include <Magnum/ImageView.h>
#include <Magnum/Magnum.h>
#include <Magnum/Math/Angle.h>
#include <Magnum/Platform/Sdl2Application.h>
#include <Magnum/Text/AbstractFont.h>
#include <Magnum/Trade/AbstractImporter.h>
#include <Magnum/Trade/ImageData.h>
#include <SDL_hints.h>
#include <SDL_syswm.h> // To get windows API handle

#include "build_info.h"
#include "csgo_integration/Gsi.h"
#include "csgo_integration/Handler.h"
#include "csgo_integration/RemoteConsole.h"
#include "csgo_parsing/AssetFinder.h"
#include "csgo_parsing/BspMapParsing.h"
#include "gui/Gui.h"
#include "InputHandler.h"
#include "rendering/BigTextRenderer.h"
#include "rendering/WorldRenderer.h"
#include "SavedUserDataHandler.h"
#include "sim/Server.h"
#include "GitHubChecker.h"

#include "BulletPhysicsTest.h"

// Allow window on a resolution of 800x600
#define MIN_WINDOW_WIDTH  768
#define MIN_WINDOW_HEIGHT 432

#define RESOURCE_GROUP_NAME "game-data" // Name used in .conf resource file

// Important: Keep these gamestate macros in sync with the values inside
// res/gsi/gamestate_integration_DZSimulator.cfg
#define GSI_HOST "127.0.0.1"
#define GSI_PORT 34754
#define GSI_AUTH_TOKEN "VXsNuRfF8VQ"

#define RCON_HOST "127.0.0.1"
#define RCON_PORT 34755

#define CLIENT_INTERPOLATION true

using namespace Magnum;
using namespace Math::Literals;

class DZSimApplication: public Platform::Application {
    public:
        explicit DZSimApplication(const Arguments& arguments);
        ~DZSimApplication();

        // List resources first so it's usable in other members' constructor
        Utility::Resource _resources; // Resources compiled into the executable

        // List plugin managers first to make sure they're already initialized
        // and usable when passed to other member's constructors.
        PluginManager::Manager<Text::AbstractFont> _font_plugin_mgr;

        // _gui_state must be one of the first declared members because its
        // state must be loaded from file and then get passed to various other
        // members' constructor.
        gui::GuiState _gui_state;
        gui::Gui _gui;

        sim::Server _gameServer;
        InputHandler _inputs;
        
        rendering::BigTextRenderer _big_text_renderer;

        // The latest world state calculated from the server
        // -> changes every server tick
        sim::WorldState _currentServerWorldState;
        // The latest client world state, interpolated and predicted by the
        // client based on the latest server world state and latest client input
        // -> changes every client frame
        sim::WorldState _currentClientWorldState;

        // Game inputs of current client frame to be sent to the server
        sim::PlayerInputState _currentGameInput;
        // All game inputs since the latest server world state, in chronological
        // order, remembered for client-side prediction
        std::vector<sim::PlayerInputState> _prevGameInputs;

        csgo_integration::RemoteConsole _csgo_rcon; // Needs to be declared before _csgo_handler
        csgo_integration::Handler _csgo_handler;
        csgo_integration::Handler::CsgoServerTickData _latest_csgo_server_data;
        csgo_integration::Handler::CsgoClientsideData _latest_csgo_client_data;

        csgo_integration::Gsi _gsi;
        csgo_integration::GsiState _latest_gsi_state;
        size_t _num_received_gsi_states = 0;

        GitHubChecker _update_checker;

        rendering::WorldRenderer _world_renderer;

        enum UserInputMode{
            // User can navigate the menu with their mouse cursor
            MENU,
            // The mouse cursor is hidden and the user moves the in-game player
            // with mouse and keyboard
            FIRST_PERSON
        };
        UserInputMode _user_input_mode;

        Vector3 _cam_pos; // X, Y, Z
        Vector3 _cam_ang; // pitch, yaw, roll

        // State recently set to avoid redundant system calls
        float cur_overlay_transparency = 0.0f; // 0 to 100, currently set window transparency
        bool cur_vsync_enabled = false; // Whether or not VSync is currently enabled
        bool is_window_click_through = false;
        gui::GuiState::VideoSettings::WindowMode cur_window_mode;
        gui::GuiState::VideoSettings::AvailableDisplay cur_fullscreen_display; // Currently used display, if in "Fullscreen Windowed" mode

        HWND win_handle = nullptr; // Windows API window handle



    private:
        void exitEvent(ExitEvent& event) override;

        bool RefreshAvailableDisplays();
        void LoadWindowIcon(Utility::Resource& res);
        void DoCsgoPathSearch(bool show_popup_on_fail=true);
        void UpdateGuiCsgoMapPaths();

        // Loads '.bsp' map files, by default from an external file. If
        // 'load_from_embedded_files' is set to true, the map file is loaded
        // from an embedded file (that was compiled into the executable).
        bool LoadBspMap(std::string file_path, bool load_from_embedded_files=false);

        void ConfigureGameKeyBindings();

        void viewportEvent(ViewportEvent& event) override;
        void tickEvent() override;
        void CalcViewProjTransformation();
        void drawEvent() override;
        void textInputEvent(TextInputEvent& event) override
        {
            if(_user_input_mode == UserInputMode::MENU)
                if (_gui._context.handleTextInputEvent(event)) return;
        }
        void mousePressEvent  (MouseEvent& event)       override
        {
            if (_user_input_mode == UserInputMode::MENU)
                if (_gui._context.handleMousePressEvent(event)) return;
            _inputs.HandleMousePressEvent(event);
        }
        void mouseReleaseEvent(MouseEvent& event)       override
        {
            if (_user_input_mode == UserInputMode::MENU)
                if (_gui._context.handleMouseReleaseEvent(event)) return;
            _inputs.HandleMouseReleaseEvent(event);
        }
        void mouseMoveEvent   (MouseMoveEvent& event)   override
        {
            if (_user_input_mode == UserInputMode::MENU)
                if (_gui._context.handleMouseMoveEvent(event)) return;
            _inputs.HandleMouseMoveEvent(event);
        }
        void mouseScrollEvent (MouseScrollEvent& event) override
        {
            if (_user_input_mode == UserInputMode::MENU)
                if (_gui._context.handleMouseScrollEvent(event)) {
                    event.setAccepted(); // Prevent scrolling the page
                    return;
                }
            _inputs.HandleMouseScrollEvent(event);
        }
        void keyPressEvent    (KeyEvent& event) override
        {
            // Allow user to close popup in case the popup's close button is
            // off-screen. While a popup is open, ImGui consumes all key events.
            if (_gui_state.popup.IN_visible) {
                if (event.key() == KeyEvent::Key::Esc) {
                    // Signal to our GUI code to close the current popup
                    _gui_state.popup.OUT_close_current = true;
                    return;
                }
            }

            if (event.key() == KeyEvent::Key::F11) {
                // Toggle window mode GUI setting
                if (_gui_state.video.IN_window_mode == gui::GuiState::VideoSettings::WINDOWED)
                    _gui_state.video.IN_window_mode = gui::GuiState::VideoSettings::FULLSCREEN_WINDOWED;
                else
                    _gui_state.video.IN_window_mode = gui::GuiState::VideoSettings::WINDOWED;
                return;
            }

            if (_user_input_mode == UserInputMode::MENU) {
                if (_gui._context.handleKeyPressEvent(event))
                    return;
            }
            _inputs.HandleKeyPressEvent(event);
        }
        void keyReleaseEvent  (KeyEvent& event) override {
            if (_user_input_mode == UserInputMode::MENU)
                if (_gui._context.handleKeyReleaseEvent(event)) return;
            _inputs.HandleKeyReleaseEvent(event);
        }
        
        std::shared_ptr<csgo_parsing::BspMap> _bsp_map;

        Matrix4 _view_proj_transformation; // = projection_matrix * view_matrix
};

DZSimApplication::DZSimApplication(const Arguments& arguments)
    : Platform::Application{ arguments, NoCreate }
    , _resources{ RESOURCE_GROUP_NAME }
    , _gui_state{ SavedUserDataHandler::LoadUserSettingsFromFile() }
    , _gui{ *this , _resources, _gui_state }
    , _gameServer{ CSGO_TICKRATE }
    , _big_text_renderer { *this,  _font_plugin_mgr }
    , _csgo_handler { _resources, _csgo_rcon, _gui_state }
    , _world_renderer { _resources, _gui_state }
    , _user_input_mode { UserInputMode::MENU }
{
    do_bullet_physics_test();

    // Save immediately to file for the sole purpose of ensuring the
    // settings file and its directory exist.
    SavedUserDataHandler::SaveUserSettingsToFile(_gui_state);

    auto& window_mode = _gui_state.video.IN_window_mode; // Attempt to start with this window mode
    auto& available_displays = _gui_state.video.OUT_available_displays;
    auto& selected_display_idx = _gui_state.video.IN_selected_display_idx; // Caution! Index can be invalid

    bool set_window_pos_on_startup = false;
    Vector2i window_pos; // Used if set_window_pos_on_startup is true
    Vector2i window_size;

    if (!RefreshAvailableDisplays()) // Results are put in available_displays
        Debug{} << "ERROR: Retrieving available displays failed!";

    bool no_display_info = available_displays.empty();

    if (no_display_info) // "Fullscreen Windowed" requires display size info
        window_mode = gui::GuiState::VideoSettings::WINDOWED;

    if (window_mode == gui::GuiState::VideoSettings::FULLSCREEN_WINDOWED) {
        assert(available_displays.size() > 0);
        // Select the first available display if selected index is invalid
        if(selected_display_idx < 0 || selected_display_idx >= available_displays.size())
            selected_display_idx = 0;
        const auto& first_display = available_displays[selected_display_idx];
        window_pos  = { first_display.x, first_display.y };
        window_size = { first_display.w, first_display.h };
        set_window_pos_on_startup = true;
    }
    else { // window_mode == gui::GuiState::VideoSettings::WINDOWED
        window_mode = gui::GuiState::VideoSettings::WINDOWED; // making sure
        
        if (no_display_info) { // No display info -> Just create a small window
            window_size = { MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT };
            set_window_pos_on_startup = false;
        }
        else { // Create window on first available display
            const float WINDOW_DISPLAY_RATIO = 0.6f;
            const auto& first_display = available_displays[0];
            window_size = {
                (Magnum::Int)(WINDOW_DISPLAY_RATIO * first_display.w),
                (Magnum::Int)(WINDOW_DISPLAY_RATIO * first_display.h)
            };
            window_pos = {
                first_display.x + (first_display.w - window_size.x()) / 2,
                first_display.y + (first_display.h - window_size.y()) / 2
            };
            set_window_pos_on_startup = true;
        }
    }

    // Remember the current window state to only react when settings change
    cur_window_mode = window_mode;
    if (cur_window_mode == gui::GuiState::VideoSettings::FULLSCREEN_WINDOWED)
        cur_fullscreen_display = available_displays[selected_display_idx];

    std::string window_title =
        "Danger Zone Simulator v" + std::string(build_info::GetVersionStr());

    // Don't add WindowFlag::Borderless to this config! It breaks window
    // transparency for unknown reasons.
    auto dpi_policy = Configuration::DpiScalingPolicy::Default;
    Configuration app_conf = Configuration{}
        .setTitle(window_title)
        .setSize(window_size, dpi_policy);

    if (window_mode != gui::GuiState::VideoSettings::FULLSCREEN_WINDOWED)
        app_conf.addWindowFlags(Configuration::WindowFlag::Resizable);

    // CURRENT LIMITATIONS WITH "FULLSCREEN WINDOWED" ON HIGHDPI DISPLAYS:
    //  - "Fullscreen Windowed" always works on SDL's display at index 0, no
    //    matter the HiDPI scaling
    //  - "Fullscreen Windowed" works on any other SDL display too, as long as
    //    it has the same HiDPI scaling as SDL's display at index 0
    //  - "Fullscreen Windowed" might not work on SDL displays at a non-zero
    //    index, if their HiDPI scaling differs from that of SDL's display at
    //    index 0. Undesired window size, position and UI scaling is the result.
    //
    // Most people play on display 0 and don't have different HiDPI scalings for
    // different monitors, so I call it good enough. A warning and advice about
    // these "Fullscreen Windowed" problems is put in the UI for the user.
    // 
    // To fix these problems, Magnum's DPI handling would probably need to be
    // modified / replaced ...
    // That is so cross-platform though, that I don't want to touch it.

    // NOTE: In Magnum's current implementation (commit v2020.06-1597-g9c4f2ceea),
    //       Sdl2Application::dpiScaling(config) determines DPI scaling only from
    //       display index 0 ! Other displays might have different DPI scaling!
    Vector2 dpi_scaling = dpiScaling(app_conf);
    app_conf.setSize(window_size / dpi_scaling, dpi_policy);

    // Try creating a context with MSAA.
    // Higher MSAA sample counts significantly reduce FPS. A sample count of 2
    // does a good job compared to no Anti-Aliasing at all.
    const Magnum::Int MSAA_SAMPLE_COUNT = 2;
    GLConfiguration glConf;
    glConf.setSampleCount(MSAA_SAMPLE_COUNT);
    if (!tryCreate(app_conf, glConf)) {
        Warning{} << "[ERROR] Context creation failed with MSAA sample count of"
            << MSAA_SAMPLE_COUNT << "-> context without MSAA will be created.";
        create(app_conf, glConf.setSampleCount(0)); // 0 = no multisampling
    }

#ifdef SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH
    // Let SDL generate mouse events even if the window isn't focused
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#else
    const char* sdl_mouse_focus_hint_msg = "SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH "
        "isn't defined! Does UI react if app window isn't in focus?";
    _gui_state.QueueMsgWarn(sdl_mouse_focus_hint_msg);
    Debug{} << sdl_mouse_focus_hint_msg;
#endif

    // Disabling window borders breaks window transparency, no idea why.
    // It doesn't matter in "Fullscreen Windowed" mode anyway, leave it enabled.
    // Removing the border might help with "Fullscreen Windowed" + HiDPI issues.
    SDL_SetWindowBordered(this->window(), SDL_TRUE);

    if (set_window_pos_on_startup)
        SDL_SetWindowPosition(this->window(), window_pos.x(), window_pos.y());

    // Magnum's setMinWindowSize() scales the given size with DPI scaling!
    // We want an absolute min size, so divide it by dpi scaling before.
    Vector2i min_window_size = { MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT };
    min_window_size /= dpi_scaling;
    this->setMinWindowSize(min_window_size);

    {
        win_handle = nullptr;
        std::string hwnd_retrieve_err_msg = "";
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version); // initialize info structure with SDL version info
        if (SDL_GetWindowWMInfo(this->window(), &info)) {
            if (info.subsystem == SDL_SYSWM_WINDOWS) {
                win_handle = info.info.win.window;
                Debug{} << "Windowing subsystem = Microsoft Windows";
            }
            else {
                hwnd_retrieve_err_msg = "subsystem = " + std::to_string(info.subsystem);
                Debug{} << "ERROR: Detected windowing" << hwnd_retrieve_err_msg.c_str();
            }
        }
        else {
            hwnd_retrieve_err_msg = std::string("SDL_GetWindowWMInfo failed: ")
                + SDL_GetError();
            Debug{} << "ERROR:" << hwnd_retrieve_err_msg.c_str();
        }

        if(!win_handle)
            _gui_state.popup.QueueMsgError(
                "Failed to access Microsoft Windows specific feature!\n\n"
                "Because of this, the DZSim window can't be made click-through "
                "in overlay mode! More things might not work too.\n\n"
                "Error information: " + hwnd_retrieve_err_msg);
    }

    
    build_info::print();
    Debug{} << "Running on" << GL::Context::current().version()
        << "using" << GL::Context::current().rendererString();

    Debug{} << "-- resources:";
    for (auto e : _resources.list())
        Debug{} << "-" << e;
    
    LoadWindowIcon(_resources);

    DoCsgoPathSearch(); // Shows user an error popup on failure

    const Containers::ArrayView<const char> font_data_disp =
        _resources.getRaw("fonts/DisplayFont.ttf");
    const Containers::ArrayView<const char> font_data_mono =
        _resources.getRaw("fonts/MonoFont.ttf");

    _gui.Init(font_data_disp, font_data_mono);

    _big_text_renderer.Init(font_data_disp);

    // Initialization of members that require a GL context to be active
    _world_renderer.InitShaders();

    // Enable transparency
    GL::Renderer::enable(GL::Renderer::Feature::Blending);

    // Blend functions for BigTextRenderer
    GL::Renderer::setBlendFunction(
        GL::Renderer::BlendFunction::One,
        GL::Renderer::BlendFunction::OneMinusSourceAlpha);
    GL::Renderer::setBlendEquation(
        GL::Renderer::BlendEquation::Add,
        GL::Renderer::BlendEquation::Add);

    ///////////////////////

    GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);
    // Clockwise, same winding order as bsp faces
    GL::Renderer::setFrontFace(GL::Renderer::FrontFace::ClockWise);
    // Only draw front faces
    GL::Renderer::setFaceCullingMode(GL::Renderer::PolygonFacing::Back);

    GL::Renderer::setClearColor(0x18214C_rgbf);// 0x32434C_rgbf);

    // Set VSync. Swap interval can be:
    // 0: VSync OFF
    // 1: VSync ON
    // -1: Adaptive VSync (on some systems)
    const bool try_enable_vsync = _gui_state.video.IN_vsync_enabled; // Check user setting
    if (!this->setSwapInterval(try_enable_vsync ? 1 : 0)) { // Driver error
        cur_vsync_enabled = !try_enable_vsync;
        Warning{} << "ERROR: setSwapInterval() failed.";
    }
    else { // Success
        cur_vsync_enabled = try_enable_vsync;
    }
    _gui_state.video.IN_vsync_enabled = cur_vsync_enabled; // Update GUI

    ConfigureGameKeyBindings();
    CalcViewProjTransformation();

    _update_checker.StartAsyncUpdateAndMotdCheck();

    _gameServer.ChangeSimulationTimeScale(1.0);// 0.015625);
    _gameServer.Start();

    // Load embedded map on startup (if it exists)
    //LoadBspMap("embedded_maps/XXX.bsp", true);
}

DZSimApplication::~DZSimApplication()
{
    // This destructor is always the last thing that gets called upon
    // program termination.

    // Joining threads prematurely in a certain order might speed up exit
    // -> Initiate thread joins in a non-blocking way here
    _csgo_rcon.Disconnect(); // Non-blocking, thread is joined in its destructor

    // Do some other potentially blocking work
    SavedUserDataHandler::SaveUserSettingsToFile(_gui_state);

    // Join all remaining threads in a blocking way
    _gameServer.Stop(); // Blocking, thread is joined
}


void DZSimApplication::exitEvent(ExitEvent& event)
{
    // exitEvent() gets called when the user presses the window close button
    // or presses Alt+F4 on our window.

    // CAUTION: This method doesn't get called when the app is closed using
    //          Magnum::Platform::Sdl2Application::exit() !
    //          -> Make sure every member can safely be destructed and user data
    //             is written to file without exitEvent() getting called!

    Debug{} << "EXIT EVENT!";
    event.setAccepted(); // Confirm exit, don't suppress it
}

// Returns false if no info of any display could be retrieved, true otherwise.
// Also tries to select the display that was selected previously. If that fails,
// the selected display index get set to -1 .
bool DZSimApplication::RefreshAvailableDisplays()
{
    using Display = gui::GuiState::VideoSettings::AvailableDisplay;
    auto& vid = _gui_state.video;

    // Try to keep the previously selected display selected
    int prev_selected_idx = vid.IN_selected_display_idx;
    Display prev_selected = { -1, -1, -1, -1 };
    bool have_prev_selected = prev_selected_idx >= 0 &&
        prev_selected_idx < vid.OUT_available_displays.size();
    if(have_prev_selected)
        prev_selected = vid.OUT_available_displays[prev_selected_idx];

    vid.OUT_available_displays.clear();
    vid.IN_selected_display_idx = -1;

    int num_displays = SDL_GetNumVideoDisplays();
    if (num_displays < 1) {
        Debug{} << "ERROR: SDL_GetNumVideoDisplays() failed:" << SDL_GetError();
        return false;
    }

    SDL_Rect disp_bounds;
    Display d;
    for (int i = 0; i < num_displays; i++) {
        // By the way, display index 0 is always located at 0,0
        if (SDL_GetDisplayBounds(i, &disp_bounds) != 0) {
            Debug{} << "ERROR: SDL_GetDisplayBounds() failed:" << SDL_GetError();
            continue;
        }
        // Select the previously selected display again if possible
        if (have_prev_selected) {
            if (prev_selected.x == disp_bounds.x &&
                prev_selected.y == disp_bounds.y &&
                prev_selected.w == disp_bounds.w &&
                prev_selected.h == disp_bounds.h) {
                vid.IN_selected_display_idx =
                    _gui_state.video.OUT_available_displays.size();
            }
        }
        // Add to available display list
        d.x = disp_bounds.x;
        d.y = disp_bounds.y;
        d.w = disp_bounds.w;
        d.h = disp_bounds.h;
        _gui_state.video.OUT_available_displays.push_back(d);
    }

    // Special case to ensure DZSimulator re-opens on the monitor that
    // DZSimulator was last closed on:
    // When we started off with a previously selected display index but no
    // associated display info (x,y,w,h), try to use the previous display index
    // in the new "available display" list.
    if (vid.IN_selected_display_idx == -1) {
        if (prev_selected_idx >= 0 &&
            prev_selected_idx < _gui_state.video.OUT_available_displays.size()) {
            vid.IN_selected_display_idx = prev_selected_idx;
        }
    }

    if (_gui_state.video.OUT_available_displays.empty())
        return false;
    else
        return true;
}

void DZSimApplication::LoadWindowIcon(Utility::Resource& res)
{
    PluginManager::Manager<Trade::AbstractImporter> manager;
    Containers::Pointer<Trade::AbstractImporter> importer =
        manager.loadAndInstantiate("PngImporter");

    if (!importer) {
        Error{} << "manager.loadAndInstantiate(\"PngImporter\") failed";
        return;
    }
    
    if (!importer->openData(res.getRaw("icons/window_icon.png"))) {
        Error{} << "importer->openData() failed";
        return;
    }

    Containers::Optional<Trade::ImageData2D> image = importer->image2D(0);
    if (!image) {
        Error{} << "Importing the image failed";
        return;
    }

    setWindowIcon(*image);
}

void DZSimApplication::DoCsgoPathSearch(bool show_popup_on_fail)
{
    auto ret = csgo_parsing::AssetFinder::FindCsgoPath();
    if (ret) // success, csgo path was found
        return;

    Debug{} << "[AssetFinder] FAILED to find CSGO install dir. Error" <<
        ret.code << "with message:" << ret.desc_msg.c_str();

    if (show_popup_on_fail) {
        std::string user_msg;
        if (ret.code == csgo_parsing::utils::RetCode::STEAM_NOT_INSTALLED)
            user_msg = "Steam and CS:GO don't seem to be installed!";
        else if (ret.code == csgo_parsing::utils::RetCode::CSGO_NOT_INSTALLED)
            user_msg = "CS:GO doesn't seem to be installed!";
        else
            user_msg = "An error occured while trying to find CS:GO's "
            "installation location: AssetFinder error "
            + std::to_string(ret.code) + "\n\n'" + ret.desc_msg + "'";

        user_msg += "\n\nIt's required to have Steam and CS:GO installed. "
            "Their installation location must be accessed by this app in order "
            "to load maps and their assets correctly!\n\nYou can still try to "
            "partially load '.bsp' files though.";

        _gui_state.popup.QueueMsgError(user_msg);
    }
}

void DZSimApplication::UpdateGuiCsgoMapPaths() {
    const size_t total_map_count =
        csgo_parsing::AssetFinder::GetMapFileList().size();

    // Separate indices of dz_* maps, *bump* maps and all other maps
    std::vector<size_t> dz_map_indices, bump_map_indices, other_map_indices;
    dz_map_indices   .reserve(total_map_count);
    bump_map_indices .reserve(total_map_count);
    other_map_indices.reserve(total_map_count);
    for (size_t i = 0; i < total_map_count; i++) {
        Corrade::Containers::StringView file_name =
            Corrade::Utility::Path::split(
                csgo_parsing::AssetFinder::GetMapFileList()[i]).second();
        if (file_name.size() >= 2
            && (file_name[0] == 'd' || file_name[0] == 'D')
            && (file_name[1] == 'z' || file_name[1] == 'Z'))
            dz_map_indices.push_back(i);
        else if (file_name.contains("bump")
            || file_name.contains("Bump")
            || file_name.contains("BUMP"))
            bump_map_indices.push_back(i);
        else
            other_map_indices.push_back(i);
    }
    // Copy all map paths into a new list, ordered by previous separation
    std::vector<std::string> gui_path_list;
    gui_path_list.reserve(total_map_count);

    for (size_t i : dz_map_indices)
        gui_path_list.push_back(csgo_parsing::AssetFinder::GetMapFileList()[i]);
    for (size_t i : bump_map_indices)
        gui_path_list.push_back(csgo_parsing::AssetFinder::GetMapFileList()[i]);
    for (size_t i : other_map_indices)
        gui_path_list.push_back(csgo_parsing::AssetFinder::GetMapFileList()[i]);

    // Copy game and map paths into GUI's own variables to work with them
    _gui_state.map_select.OUT_csgo_path = csgo_parsing::AssetFinder::GetCsgoPath();
    _gui_state.map_select.OUT_loadable_maps = std::move(gui_path_list);
    _gui_state.map_select.OUT_num_highlighted_maps = dz_map_indices.size();
}

// Returns success
bool DZSimApplication::LoadBspMap(std::string file_path,
    bool load_from_embedded_files)
{
    // Deallocate previous map data to minimize peak RAM usage during parsing
    // RAM USAGE NOT REALLY TESTED YET!
    _bsp_map.reset();
    _world_renderer.UnloadGeometry();

    // Embedded map files must not rely on assets from the game directory.
    // -> Indexing game directory assets for them is unnecessary
    if (!load_from_embedded_files) {
        // Reload VPK archives, in case they were just updated by Steam
        // Only index files with extensions that we need -> Reduces VPK index time
        std::vector<std::string> required_file_ext = { "mdl", "phy" };
        csgo_parsing::AssetFinder::RefreshVpkArchiveIndex(required_file_ext);
    }

    Debug{} << "Loading" << (load_from_embedded_files ? "embedded" : "regular")
        << "map file:" << file_path.c_str();
    csgo_parsing::utils::RetCode bsp_parse_status;
    if (load_from_embedded_files) {
        bool embedded_file_exists = false;
        for (Containers::StringView res : _resources.list()) {
            if (res == file_path) {
                embedded_file_exists = true;
                break;
            }
        }
        if (embedded_file_exists) {
            auto file_content = Containers::arrayCast<const uint8_t>(
                _resources.getRaw(file_path)
            );
            bsp_parse_status = csgo_parsing::ParseBspMapFile(&_bsp_map, file_content);
        }
        else {
            // Embedded file doesn't exist. We don't show an error message in
            // this case because the developer simply might have decided to not
            // include or use an embedded map file on startup, which the user
            // shouldn't be notified of.
            Debug{} << "EMBEDDED MAP FILE IS MISSING!";
            return false;
        }
    }
    else {
        bsp_parse_status = csgo_parsing::ParseBspMapFile(&_bsp_map, file_path);
    }

    if (!bsp_parse_status.successful()) { // Parse error
        std::string msg = "Failed to load the map:\n\n" + bsp_parse_status.desc_msg;
        Error{} << "ERROR:" << msg.c_str();
        _gui_state.popup.QueueMsgError(msg);
        return false;
    }

    // There might be warnings from parsing the BSP file
    if (!bsp_parse_status.desc_msg.empty())
        _gui_state.popup.QueueMsgWarn(bsp_parse_status.desc_msg);

    _world_renderer.LoadBspMapGeometry(_bsp_map);

    if (_bsp_map->player_spawns.size() > 0) {
        csgo_parsing::BspMap::PlayerSpawn& playerSpawn =
            _bsp_map->player_spawns[0];
        _cam_pos = playerSpawn.origin;
        _cam_ang = playerSpawn.angles;
        _currentClientWorldState.player.position = playerSpawn.origin;
        _currentClientWorldState.player.angles = playerSpawn.angles;
    }
    _currentClientWorldState.time = sim::Clock::now();
    _currentClientWorldState.latest_player_input_time =
        _currentClientWorldState.time;
    _gameServer.OverrideWorldState(_currentClientWorldState);
    _currentServerWorldState = _currentClientWorldState;

    Debug{} << "DONE loading bsp map";
    return true;
}

void DZSimApplication::ConfigureGameKeyBindings() {
    _inputs.SetKeyPressedCallback_keyboard("W", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_FORWARD); });
    _inputs.SetKeyReleasedCallback_keyboard("W", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_FORWARD); });
  
    _inputs.SetKeyPressedCallback_keyboard("S", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_BACK); });
    _inputs.SetKeyReleasedCallback_keyboard("S", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_BACK); });

    _inputs.SetKeyPressedCallback_keyboard("A", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_MOVELEFT); });
    _inputs.SetKeyReleasedCallback_keyboard("A", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_MOVELEFT); });

    _inputs.SetKeyPressedCallback_keyboard("D", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_MOVERIGHT); });
    _inputs.SetKeyReleasedCallback_keyboard("D", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_MOVERIGHT); });

    _inputs.SetKeyPressedCallback_keyboard("E", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_USE); });
    _inputs.SetKeyReleasedCallback_keyboard("E", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_USE); });

    _inputs.SetKeyPressedCallback_keyboard("Space", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_JUMP); });
    _inputs.SetKeyReleasedCallback_keyboard("Space", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_JUMP); });

    _inputs.SetKeyPressedCallback_keyboard("Left Ctrl", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_DUCK); });
    _inputs.SetKeyReleasedCallback_keyboard("Left Ctrl", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_DUCK); });

    _inputs.SetKeyPressedCallback_keyboard("Left Shift", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_SPEED); });
    _inputs.SetKeyReleasedCallback_keyboard("Left Shift", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_SPEED); });

    _inputs.SetKeyPressedCallback_mouse("MButtonLeft", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_ATTACK); });
    _inputs.SetKeyReleasedCallback_mouse("MButtonLeft", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_ATTACK); });

    _inputs.SetKeyPressedCallback_mouse("MButtonRight", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_ATTACK2); });
    _inputs.SetKeyReleasedCallback_mouse("MButtonRight", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_ATTACK2); });

    // ----

    _inputs.SetKeyPressedCallback_mouse("MWheelDown", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_JUMP); });
    _inputs.SetKeyReleasedCallback_mouse("MWheelDown", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_JUMP); });

    _inputs.SetKeyPressedCallback_mouse("MWheelUp", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::PLUS_JUMP); });
    _inputs.SetKeyReleasedCallback_mouse("MWheelUp", [this]() {
        this->_currentGameInput.inputCommands.push_back(sim::PlayerInputState::Command::MINUS_JUMP); });

}

void DZSimApplication::viewportEvent(ViewportEvent& event)
{
    Debug{ Debug::Flag::NoSpace} << "[VIEWPORTEVENT]"
        << " window="
        << "(" << windowSize().x() << "," << windowSize().y() << ")"
        << ", framebuffer="
        << "(" << framebufferSize().x() << "," << framebufferSize().y() << ")"
        << ", dpi scaling="
        << "(" << dpiScaling().x() << "," << dpiScaling().y() << ")";

    GL::defaultFramebuffer.setViewport({{}, windowSize()});
    CalcViewProjTransformation();

    // Pass new framebuffer size to apis handling user events or scale UI
    // elements
    _gui.HandleViewportEvent(event);
    _big_text_renderer.HandleViewportEvent(event);

    redraw();
}

// Called after processing all input events and before drawEvent()
void DZSimApplication::tickEvent() {
    // All mouse and key events have been processed right before calling
    // tickEvent() -> Save the time point when the game input was sampled
    auto current_time = sim::Clock::now();
    _currentGameInput.time = current_time;

    if (_gui_state.app_exit_requested) // Exit if user requested it
        this->exit(); // CAUTION: This way, exitEvent() doesn't get called!

    auto window_flags = SDL_GetWindowFlags(this->window());
    bool is_window_focused = window_flags & SDL_WINDOW_INPUT_FOCUS;

    // When we're in "sparing low latency draw mode" the following happens:
    //   - Targeted main loop frequency is set to 1000 Hz (i.e. tickEvent() rate)
    //   - VSync is disabled, ignoring user setting
    //   - A new frame is only drawn when needed (e.g. once new data has arrived)
    //   - When frames are not required to be drawn frequently enough, they're
    //     drawn at a fixed, low frame rate
    //
    // This is useful for 2 situations: In order to reduce visual delay between
    // CSGO and DZSimulator when it's used as an overlay on top of CSGO, we want
    // to draw a new frame ASAP after new movement data from CSGO was received.
    // Apart from that, this mode saves computer resources by drawing frames at
    // a low rate when nothing important needs to be drawn and DZSimulator's
    // window is not focused.
    //
    // When we're NOT in "sparing low latency draw mode" the following happens:
    //   - New frames are always drawn
    //   - VSync and FPS limit are enabled/set according to user settings
    bool is_sparing_low_latency_draw_mode_enabled = !is_window_focused;

    // If a new frame must be drawn after this tickEvent()
    bool redraw_needed = false;

    if (!is_sparing_low_latency_draw_mode_enabled)
        redraw_needed = true; // Here, always draw new frames

    // Communicate with csgo console if connected and process its data
    _csgo_handler.Update();
    {
        auto csgo_server_data_q = _csgo_handler.DequeNewCsgoServerTicksData();
        auto csgo_client_data_q = _csgo_handler.DequeNewCsgoClientsideData();

        // In low latency draw mode, draw a new frame only when necessary
        if (is_sparing_low_latency_draw_mode_enabled)
            // Draw a new frame once we have new client-side position and view
            // angles from CSGO
            if (!csgo_client_data_q.empty())
                redraw_needed = true;

        // Process new CSGO server ticks
        for (auto& server_tick_data : csgo_server_data_q)
            _latest_csgo_server_data = std::move(server_tick_data);

        // Process new CSGO client-side data
        for (auto& clientside_data : csgo_client_data_q)
            _latest_csgo_client_data = std::move(clientside_data);
    }

    bool esc_pressed = _inputs.GetKeyPressCountAndReset_keyboard("Escape");
    bool leaving_first_person_mode = false;
    // If GUI popup appeared, allow cursor to directly close -> enter menu mode
    if (_gui_state.popup.IN_visible) {
        if (_user_input_mode != UserInputMode::MENU) {
            if (_user_input_mode == UserInputMode::FIRST_PERSON)
                leaving_first_person_mode = true;
            _user_input_mode = UserInputMode::MENU;
            setCursor(Cursor::Arrow);
        }
    }
    else if (esc_pressed) { // Toggle modes with ESC key
        if (_user_input_mode == UserInputMode::MENU) {
            _user_input_mode = UserInputMode::FIRST_PERSON;
            setCursor(Cursor::HiddenLocked);
        }
        else if(_user_input_mode == UserInputMode::FIRST_PERSON) {
            _user_input_mode = UserInputMode::MENU;
            setCursor(Cursor::Arrow);
            leaving_first_person_mode = true;
        }
    }
    // Delete input commands if we haven't been in first person mode
    if (_user_input_mode != UserInputMode::FIRST_PERSON)
        _currentGameInput.inputCommands.clear();
    // Send all possible MINUS_* player input commands to stop ingame input
    // after leaving first person control
    if (leaving_first_person_mode) {
        auto minus_cmd_list = sim::PlayerInputState::AllMinusCommands();
        for (sim::PlayerInputState::Command c : minus_cmd_list)
            _currentGameInput.inputCommands.push_back(c);
    }
    // Let GUI know about the current input mode
    _gui_state.ctrl_help.OUT_first_person_control_active =
        _user_input_mode == UserInputMode::FIRST_PERSON;

    // Handle DZSimulator GitHub update checking
    if (_gui_state.IN_open_downloads_page_in_browser) {
        _gui_state.IN_open_downloads_page_in_browser = false;
        GitHubChecker::OpenDZSimUpdatePageInBrowser();
    }
    if (!_update_checker.IsAsyncUpdateAndMotdCheckFinished()) {
        _gui_state.OUT_dzsim_update_available = false;
    }
    else {
        static bool s_was_check_result_processed = false;
        if (!s_was_check_result_processed) {
            s_was_check_result_processed = true;

            std::string motd = _update_checker.GetMotd();
            if (!motd.empty())
                _gui_state.popup.QueueMsgInfo(motd);

            switch (_update_checker.GetUpdateStatus()) {
            case GitHubChecker::UpdateStatus::UPDATE_AVAILABLE:
                Debug{} << "New update available on GitHub!";
                _gui_state.popup.QueueMsgInfo("NEW UPDATE AVAILABLE!\n\n"
                    "A new version of DZSimulator was published on GitHub (It "
                    "might have new useful features).\n\n"
                    "To check it out, press the \"Open downloads page\" button "
                    "in the menu.\n\n"
                    "Alternatively, you can visit "
                    "https://github.com/lacyyy/DZSimulator/releases");
                _gui_state.OUT_dzsim_update_available = true;
                break;
            case GitHubChecker::UpdateStatus::NOT_CHECKED:
            case GitHubChecker::UpdateStatus::NO_UPDATE_AVAILABLE:
                Debug{} << "No update available on GitHub!";
                _gui_state.OUT_dzsim_update_available = false;
                break;
            case GitHubChecker::UpdateStatus::UPDATE_CHECK_FAILED:
            default:
                Debug{} << "An error occurred while checking for updates on "
                    "GitHub";
                _gui_state.OUT_dzsim_update_available = false;
                break;
            }
        }
    }

    float wanted_transparency = 0.0f;
    if (_gui_state.video.IN_overlay_mode_enabled) {
        if (_gui_state.video.IN_overlay_transparency_is_being_adjusted
            || !is_window_focused) // Use setting value when previewing or when not focused
            wanted_transparency = _gui_state.video.IN_overlay_transparency;
        else // Very subtle indicator for user that shows we are in overlay mode
            wanted_transparency = 10.0f;
    }

    // Only call SDL opacity function when necessary
    if (wanted_transparency != cur_overlay_transparency) {
        cur_overlay_transparency = wanted_transparency; // Only try setting opacity once
        float win_opacity = 1.0f - (0.01f * wanted_transparency);
        // CAUTION: On Windows, setting a window opacity of 1.0 or greater causes
        // the window-click-through feature to stop working. This is due to the SDL
        // implementation removing the WS_EX_LAYERED flag from the extended window
        // styles whenever an opacity of 1.0 is set. That flag is necessary to make
        // the window click-through. Allowing click-through with an opacity of 1.0
        // doesn't make sense anyway, but be aware.
        // Edit: In later tests, an opacity of 1.0 no longer seems to break it...
        //if (win_opacity > 0.99f)
        //    win_opacity = 0.99f; // Avoid breaking "click-through" feature
        if (SDL_SetWindowOpacity(this->window(), win_opacity) != 0) {
            Debug{} << "ERROR: SDL_SetWindowOpacity() failed!";
            _gui_state.popup.QueueMsgError("An error occurred while trying to "
                "set the window's transparency!\nTransparent windows might not "
                "be supported on this system!");
        }
    }

    // Set window "always in foreground" mode if we are in overlay mode
    SDL_SetWindowAlwaysOnTop(this->window(),
        _gui_state.video.IN_overlay_mode_enabled ? SDL_TRUE : SDL_FALSE);

    if (win_handle) {
        static bool s_has_any_exstyle_op_failed = false;
        std::string exstyle_op_err_msg = "";
        LONG_PTR win_exstyle = GetWindowLongPtr(win_handle, GWL_EXSTYLE);
        if (win_exstyle == 0) {
            exstyle_op_err_msg = "GetWindowLongPtr() failed";
        }
        else {
            // Make window "click-through" in overlay mode
            bool want_click_through =
                _gui_state.video.IN_overlay_mode_enabled && !is_window_focused;

            if (want_click_through && !is_window_click_through) {
                // GWL_EXSTYLE must have WS_EX_LAYERED and WS_EX_TRANSPARENT flags
                // to make window click-through
                win_exstyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
                if (SetWindowLongPtr(win_handle, GWL_EXSTYLE, win_exstyle) == 0) {
                    exstyle_op_err_msg = "SetWindowLongPtr() failed";
                }
                else {
                    is_window_click_through = true;
                    Debug{} << "Window is now click-through!";
                }
            }
            else if (!want_click_through && is_window_click_through) {
                win_exstyle &= ~WS_EX_TRANSPARENT;
                if (SetWindowLongPtr(win_handle, GWL_EXSTYLE, win_exstyle) == 0) {
                    exstyle_op_err_msg = "SetWindowLongPtr() failed";
                }
                else {
                    is_window_click_through = false;
                    Debug{} << "Window is clickable again!";
                }
            }
            // Is this necessary to make the changes take effect?
            //SetWindowPos(hWnd, HWND_TOPMOST, 100, 100, 1000, 500, SWP_SHOWWINDOW);
        }
        if (exstyle_op_err_msg.length() != 0) {
            Debug{} << "ERROR during GWL_EXSTYLE operations:" << exstyle_op_err_msg.c_str();
            if (!s_has_any_exstyle_op_failed) { // If first error here
                _gui_state.popup.QueueMsgError(
                    "Error occurred while checking/enabling/disabling this "
                    "window's click-through state. That might be messed up now.\n\n"
                    "Error info: " + exstyle_op_err_msg);
                s_has_any_exstyle_op_failed = true;
            }
        }
    }

    const auto WINDOWED            = gui::GuiState::VideoSettings::WINDOWED;
    const auto FULLSCREEN_WINDOWED = gui::GuiState::VideoSettings::FULLSCREEN_WINDOWED;
    auto& gui_window_mode_setting = _gui_state.video.IN_window_mode;
    auto& available_displays = _gui_state.video.OUT_available_displays;
    auto& selected_display_idx = _gui_state.video.IN_selected_display_idx;

    // If GUI asks to refresh display info
    if (_gui_state.video.IN_available_display_refresh_needed) {
        _gui_state.video.IN_available_display_refresh_needed = false;
        RefreshAvailableDisplays(); // Ignore possible error
    }

    // If user changed window mode setting
    if (cur_window_mode != gui_window_mode_setting) {
        if (gui_window_mode_setting == FULLSCREEN_WINDOWED) {
            // Try switching from "Windowed" to "Fullscreen Windowed"
            if (!RefreshAvailableDisplays()) {
                // Unable to switch to "Fullscreen Windowed", keep current window mode.
                _gui_state.popup.QueueMsgError("An error occurred while trying "
                    "to switch to \"Fullscreen Windowed\" mode!");
                gui_window_mode_setting = cur_window_mode;
            }
            else {
                // Setting a maximized window's size or position causes window bar bugs
                if (window_flags & SDL_WINDOW_MAXIMIZED)
                    SDL_RestoreWindow(this->window()); // Leave "Maximized" mode
                // If no display is/was selected, just select the first display
                if (selected_display_idx < 0 || selected_display_idx >= available_displays.size())
                    selected_display_idx = 0;
                const auto& display = available_displays[selected_display_idx];
                SDL_SetWindowPosition(this->window(), display.x, display.y);
                SDL_SetWindowSize    (this->window(), display.w, display.h);
                // Remember window mode and display to detect gui setting changes
                cur_window_mode = FULLSCREEN_WINDOWED;
                cur_fullscreen_display = display;
            }
        }
        else {
            // Switch from "Fullscreen Windowed" to "Windowed"
            bool prev_display_info_lost = false;
            if (!RefreshAvailableDisplays())
                prev_display_info_lost = true;
            if (selected_display_idx < 0 || selected_display_idx >= available_displays.size())
                prev_display_info_lost = true;

            if (prev_display_info_lost) {
                selected_display_idx = -1;
                // As a safe fallback, switch to small window at screen origin
                SDL_SetWindowSize(this->window(), MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);
                SDL_SetWindowPosition(this->window(), 50, 50);
            }
            else {
                // Switch to smaller window that is centered on previous display
                const float WINDOW_DISPLAY_RATIO = 0.6f;
                const auto& prev_display = available_displays[selected_display_idx];
                Vector2i new_window_size = {
                    (Magnum::Int)(WINDOW_DISPLAY_RATIO * prev_display.w),
                    (Magnum::Int)(WINDOW_DISPLAY_RATIO * prev_display.h)
                };
                SDL_SetWindowSize(this->window(), new_window_size.x(), new_window_size.y());
                SDL_SetWindowPosition(this->window(),
                    prev_display.x + (prev_display.w - new_window_size.x()) / 2,
                    prev_display.y + (prev_display.h - new_window_size.y()) / 2);
            }

            gui_window_mode_setting = WINDOWED;
            cur_window_mode = WINDOWED;
        }
    }

    // Check if user changed display selection setting while in "Fullscreen Windowed" mode
    if (cur_window_mode == FULLSCREEN_WINDOWED) {
        // If a display is selected in the GUI
        if (selected_display_idx >= 0 || selected_display_idx < available_displays.size()) {
            const auto& selected_display = available_displays[selected_display_idx];
            // If selection is different from current
            if (0
                || cur_fullscreen_display.x != selected_display.x
                || cur_fullscreen_display.y != selected_display.y
                || cur_fullscreen_display.w != selected_display.w
                || cur_fullscreen_display.h != selected_display.h)
            {
                // Move window to newly selected display
                SDL_SetWindowSize    (this->window(), selected_display.w, selected_display.h);
                SDL_SetWindowPosition(this->window(), selected_display.x, selected_display.y);
                cur_fullscreen_display = selected_display;
            }
        }
    }

    // Set correct window resizability state
    if (cur_window_mode == WINDOWED) SDL_SetWindowResizable(this->window(), SDL_TRUE);
    else                             SDL_SetWindowResizable(this->window(), SDL_FALSE);


    // Remote csgo console
    if (_gui_state.rcon.IN_disconnect
        || _gui_state.vis.IN_geo_vis_mode != _gui_state.vis.GLID_OF_CSGO_SESSION) {
        _gui_state.rcon.IN_disconnect = false;
        _csgo_rcon.Disconnect();
    }
    if (_gui_state.rcon.IN_start_connect) {
        _gui_state.rcon.IN_start_connect = false;
        bool connect_allowed =
            (!_csgo_rcon.IsConnecting() && !_csgo_rcon.IsConnected())
            || _csgo_rcon.IsDisconnecting();
        if (connect_allowed)
            _csgo_rcon.StartConnecting(RCON_HOST, RCON_PORT);
    }
    // Update remote console state for GUI
    _gui_state.rcon.OUT_is_connecting = _csgo_rcon.IsConnecting();
    _gui_state.rcon.OUT_is_connected = _csgo_rcon.IsConnected();
    _gui_state.rcon.OUT_has_connect_failed = _csgo_rcon.HasFailedToConnect();
    _gui_state.rcon.OUT_is_disconnecting = _csgo_rcon.IsDisconnecting();
    _gui_state.rcon.OUT_fail_msg = _csgo_rcon.GetLastErrorMessage();

    
    // Set max main loop frequency (value only takes effect if VSync is off)
    if (is_sparing_low_latency_draw_mode_enabled) {
        // Set targeted main loop frequency to 1000 Hz for low latency drawing
        this->setMinimalLoopPeriod(1); // Not 0, don't want to hog the CPU!
    }
    else {
        // Respect user setting for the FPS limit
        this->setMinimalLoopPeriod(_gui_state.video.IN_min_loop_period);
    }
    
    // Enable / disable VSync if required
    static bool s_vsync_error = false; // If setting VSync has failed previously
    if (!s_vsync_error) {
        bool want_vsync = is_sparing_low_latency_draw_mode_enabled ?
            false : _gui_state.video.IN_vsync_enabled;

        if (want_vsync != cur_vsync_enabled) {
            int interval = want_vsync ? 1 : 0;
            if (!this->setSwapInterval(interval)) {
                // Driver error. Set GUI to previous VSync state
                s_vsync_error = true; // Don't attempt to set VSync again
                _gui_state.video.IN_vsync_enabled = cur_vsync_enabled;
                Warning{} << "ERROR: setSwapInterval() failed.";
                if (interval == 0) _gui_state.popup.QueueMsgError("Failed to disable VSync!");
                else               _gui_state.popup.QueueMsgError("Failed to enable VSync!");
            }
            else { // Success. Update VSync state.
                cur_vsync_enabled = want_vsync;
            }
        }
    }

    // Map load selection GUI handling
    if (_gui_state.map_select.IN_box_opened) {
        _gui_state.map_select.IN_box_opened = false;
        DoCsgoPathSearch(false); // Search for CSGO's install dir again
        // Search game directory for map files
        csgo_parsing::AssetFinder::RefreshMapFileList();
        UpdateGuiCsgoMapPaths();
    }
    // Check if user selected another map file to load
    if (!_gui_state.map_select.IN_new_abs_map_path_load.empty()) {
        std::string abs_path_to_load = "";
        abs_path_to_load.swap(_gui_state.map_select.IN_new_abs_map_path_load);

        // Load new map
        // The whole client server reset logic is very hacky and needs to be tidied up
        if (LoadBspMap(abs_path_to_load)) {
            // ... Successfully loaded
        }
    }


    // Read GSI data if available
    std::optional<Vector3> latest_new_gsi_cam_angles; // initially empty
    std::optional<Vector3> latest_new_gsi_cam_pos; // initially empty
    //for (auto& gsi_state : _gsi.GetNewestGsiStates()) {
    //    //if (gsi_state.map_name.has_value())
    //    //    Magnum::Debug{} << "map =" << gsi_state.map_name.value().c_str();
    //    if (gsi_state.spec_pos.has_value())
    //        latest_new_gsi_cam_pos = gsi_state.spec_pos.value();

    //    if (gsi_state.spec_forward.has_value())
    //        latest_new_gsi_cam_angles = utils_3d::CalcAnglesFromForwardVec(
    //            gsi_state.spec_forward.value());

    //    _latest_gsi_state = std::move(gsi_state);
    //    _num_received_gsi_states++;
    //}
    //// Start/stop game state integration
    //if (_gsi.IsRunning()) {
    //    if (_gui_state.in_gsi_enabled == false)
    //        _gsi.Stop();
    //}
    //else { // If GSI is NOT running
    //    if (_gui_state.in_gsi_enabled == true) {
    //        // Reset gsi state
    //        _num_received_gsi_states = 0;
    //        _latest_gsi_state = {};
    //        if (!_gsi.Start(GSI_HOST, GSI_PORT, GSI_AUTH_TOKEN))
    //            // If starting GSI fails, disable gui state again
    //            _gui_state.in_gsi_enabled = false;
    //    }
    //}
    //// Update GSI GUI state
    //_gui_state.out_gsi_running = _gsi.IsRunning();
    //_gui_state.out_gsi_exited_unexpectedly = _gsi.HasHttpServerUnexpectedlyClosed();
    //std::string gsi_info = "Game states received: "
    //    + std::to_string(_num_received_gsi_states);
    //gsi_info += "\nmap: ";
    //if (_latest_gsi_state.map_name.has_value())
    //    gsi_info += _latest_gsi_state.map_name.value();

    //gsi_info += "\nspec pos:    ";
    //if (_latest_gsi_state.spec_pos.has_value()) {
    //    Vector3& v = _latest_gsi_state.spec_pos.value();
    //    gsi_info += std::format("({:8.2f}, {:8.2f}, {:8.2f})", v[0], v[1], v[2]);
    //}

    //gsi_info += "\nspec angles: ";
    //if (_latest_gsi_state.spec_forward.has_value()) {
    //    Vector3 v = _latest_gsi_state.spec_forward.value();
    //    v = utils_3d::CalcAnglesFromForwardVec(v); // Convert to pitch, yaw, roll
    //    gsi_info += std::format("({:6.1f}, {:6.1f}, {:6.1f})", v[0], v[1], v[2]);
    //}
    //_gui_state.out_gsi_info = gsi_info;
    //_gui_state.out_gsi_latest_json_payload = _latest_gsi_state.json_payload;

    const Float AIM_SENSITIVITY = 0.03f;

    //bool cam_input_mouse_enabled = true;
    //if (_gui_state.in_gsi_imitate_spec_cam)
    //    cam_input_mouse_enabled = false;

    // Get mouse movement
    Vector2i mouse_pos_change = _inputs.GetMousePosChangeAndReset();

    // Override with CSGO camera angles if we're connected to CSGO's console
    if (_csgo_rcon.IsConnected() &&
        _gui_state.vis.IN_geo_vis_mode == _gui_state.vis.GLID_OF_CSGO_SESSION) {
        _cam_ang = _latest_csgo_client_data.player_angles;
    }
    // Override camera angles if GSI cam imitation is enabled
    else if (_gui_state.gsi.IN_imitate_spec_cam) {
        if (latest_new_gsi_cam_angles.has_value())
            _cam_ang = latest_new_gsi_cam_angles.value();
    }
    else if (_user_input_mode == UserInputMode::FIRST_PERSON) {
        //if (cam_input_mouse_enabled) {
        if (cursor() == Cursor::HiddenLocked) {
            Vector2 delta = AIM_SENSITIVITY * Vector2{ mouse_pos_change };
            _cam_ang.x() += delta.y(); // cam pitch
            _cam_ang.y() -= delta.x(); // cam yaw
        }

        // Clamp camera angles
        if (_cam_ang.x() >  89.0f) _cam_ang.x() = 89.0f;
        if (_cam_ang.x() < -89.0f) _cam_ang.x() = -89.0f;
        // Let yaw wrap around from -180 to +180 and vice versa
        if (_cam_ang.y() > 180.0f || _cam_ang.y() < -180.0f) {
            Float overturn;
            if (_cam_ang.y() > 180.0f)
                overturn = _cam_ang.y() - 180.0f;
            else
                overturn = _cam_ang.y() + 180.0f;
            Long full_360s = overturn / 360.0f;
            overturn -= full_360s * 360.0f;

            if (_cam_ang.y() > 180.0f) _cam_ang.y() = -180 + overturn;
            else                      _cam_ang.y() = 180 + overturn;
        }
        //}
    }

    // Update viewing angles of game input for server
    _currentGameInput.viewingAnglePitch = _cam_ang.x();
    _currentGameInput.viewingAngleYaw   = _cam_ang.y();

    // Send game input to server
    _gameServer.SendNewPlayerInput(_currentGameInput);
    
    // Remember current client frame's game input for client-side prediction
    _prevGameInputs.push_back(_currentGameInput);

    // Clear client game commands for next frame's commands.
    // We keep the remaining game input values.
    _currentGameInput.inputCommands.clear();

    // Get new world states from server, if available
    std::queue<sim::WorldState> newServerWorldStates =
        _gameServer.DequeueLatestWorldStates();
    while (!newServerWorldStates.empty()) {
        _currentServerWorldState = newServerWorldStates.front();
        newServerWorldStates.pop();
    }

    // Delete all game inputs from _prevGameInputs that affected the latest
    // server world state, i.e. that have already been processed by the server
    auto firstUnprocessedInputIt = std::find_if(
        _prevGameInputs.begin(),
        _prevGameInputs.end(),
        [this](const sim::PlayerInputState& pis) {
            return pis.time >
                this->_currentServerWorldState.latest_player_input_time;
        });
    _prevGameInputs.erase(_prevGameInputs.begin(), firstUnprocessedInputIt);

    if (!CLIENT_INTERPOLATION) {
        _currentClientWorldState = _currentServerWorldState;
    }
    else {
        // Predict world states based on latest server world state and the
        // latest client input until we get a world state that's in the future
        double serverTickRate = _gameServer.GetTickRate();
        double serverSimTimeScale = _gameServer.GetSimulationTimeScale();
        double simStepSize = 1.0 / serverTickRate; // In seconds
        sim::Clock::duration serverFrameLength = std::chrono::microseconds{ (long long)(1000'000 / (serverTickRate * serverSimTimeScale)) };

        sim::WorldState predictedWorldState = _currentServerWorldState;
        auto playerInputBeginIt = _prevGameInputs.begin(); // Start of player input range used for next prediction
        sim::Clock::time_point currentTime = sim::Clock::now();
        //size_t _predictCnt = 0;
        while (predictedWorldState.time <= currentTime) { // Predict until we get a state in the future
            //++_predictCnt;
            sim::Clock::time_point nextStateTime = predictedWorldState.time + serverFrameLength;

            // Determine end of player input range of this server frame
            auto playerInputEndIt = std::find_if(playerInputBeginIt, _prevGameInputs.end(),
                [&nextStateTime](const sim::PlayerInputState& pis) {
                return pis.time > nextStateTime;
            });

            predictedWorldState.DoTimeStep(simStepSize, playerInputBeginIt, playerInputEndIt);
            predictedWorldState.time = nextStateTime; // Adjust world state time to account for simulation time scale

            playerInputBeginIt = playerInputEndIt; // Next prediction uses player input following the current player input range
        }

        //ACQUIRE_COUT(Debug{} << "pred=" << _predictCnt;)

        // Interpolate between the current client state and the predicted future server state
        auto interpRange = predictedWorldState.time - _currentClientWorldState.time;
        auto interpStep = currentTime - _currentClientWorldState.time;
        float interpRange_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(interpRange).count();
        float interpStep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(interpStep).count();
        if (interpRange_ns == 0.0f) {
            _currentClientWorldState = predictedWorldState;
        }
        else {
            float phase = interpStep_ns / interpRange_ns;
            _currentClientWorldState = sim::WorldState::Interpolate(_currentClientWorldState, predictedWorldState, phase);
        }
    }

    // Override with CS:GO camera position if we're connected to CSGO's console
    if (_csgo_rcon.IsConnected() &&
        _gui_state.vis.IN_geo_vis_mode == _gui_state.vis.GLID_OF_CSGO_SESSION) {

        // When we are in overlay mode, the client-side eye position makes for
        // a smoother overlay compared to the server-side eye position!
        _cam_pos = _latest_csgo_client_data.player_pos_eye;
    }
    // Override camera position if GSI cam imitation is enabled
    else if(_gui_state.gsi.IN_imitate_spec_cam) {
        if (latest_new_gsi_cam_pos.has_value())
            _cam_pos = latest_new_gsi_cam_pos.value() + Vector3(0, 0,
                CSGO_PLAYER_EYE_LEVEL_STANDING);
    }
    else { // Take position from our server's game state
        _cam_pos = _currentClientWorldState.player.position;
    }

    sim::Server::PerformanceStats serverPerf = _gameServer.GetPerformanceStats();
    /*std::stringstream stream;
    stream << std::fixed << std::setprecision(1) <<
        serverPerf.tick_start_deviation_rel * 100.0f;
    ACQUIRE_COUT( Debug{} << "tickStartDev:\t" << stream.str().c_str() << "%"
        << "tick:\t" << serverPerf.tickCnt
        << "sv:\t" << serverPerf.tick_duration_ms
        << "ms +-\t" << serverPerf.tick_duration_deviation_ms << "ms"
        << "MaxSleepDev:\t" << serverPerf.max_sleep_deviation_ms << "ms";)*/

    /*if (!latestWorldStates.empty())
        ACQUIRE_COUT(Debug{} << m_currentWorldState.player.position.z();)*/

    // When in "sparing low latency draw mode", force a frame redraw if the last
    // redraw was too long ago
    static sim::Clock::time_point s_last_redraw_time = current_time;
    if (is_sparing_low_latency_draw_mode_enabled && !redraw_needed) {
        // Caution: Setting the min FPS target above 64 worsens visual delay
        // between CSGO and DZSim when used as an overlay!
        const int MIN_FPS_TARGET = 15;
        const auto MAX_FRAME_INTERVAL =
            std::chrono::microseconds(1'000'000 / MIN_FPS_TARGET);

        if (current_time - s_last_redraw_time > MAX_FRAME_INTERVAL)
            redraw_needed = true;
    }

    if (redraw_needed) {
        s_last_redraw_time = current_time; // Remember last redraw time
        CalcViewProjTransformation();
        redraw();
    }
}

// CSGO's vertical FOV is fixed. CSGO's horizontal FOV depends on the screen's
// aspect ratio (which is width/height)
Matrix4 CalcCsgoPerspectiveProjection(float aspect_ratio, Magnum::Deg vert_fov) {
    const float near =     8.0f; // might not be the same as CSGO
    const float far  = 30000.0f; // might not be the same as CSGO
    return Matrix4::perspectiveProjection(
        2.0f * near * std::tan(float(Rad{ vert_fov }) * 0.5f) * Vector2::xScale(aspect_ratio), near, far);
}

void DZSimApplication::CalcViewProjTransformation() {
    Matrix4 view_transformation =
        Matrix4::rotationZ(Deg{  (_cam_ang.z()) }            ) * // camera roll
        Matrix4::rotationX(Deg{  (_cam_ang.x()) } - 90.0_degf) * // camera pitch
        Matrix4::rotationZ(Deg{ -(_cam_ang.y()) } + 90.0_degf) * // camera yaw
        Matrix4::translation(-_cam_pos);

    Deg vertical_fov = _gui_state.video.IN_use_custom_fov ?
        Deg{ _gui_state.video.IN_custom_vert_fov_degrees } : CSGO_VERT_FOV;

    // Get exact same projection like CSGO to make DZSim's image accurate when
    // used as a CSGO overlay
    Matrix4 projection_transformation = CalcCsgoPerspectiveProjection(
        Vector2{ windowSize() }.aspectRatio(),
        vertical_fov
    );

    _view_proj_transformation = projection_transformation * view_transformation;
}

void DZSimApplication::drawEvent() {
    GL::defaultFramebuffer.clear(
        GL::FramebufferClear::Color | GL::FramebufferClear::Depth);


    Vector3 player_feet_pos;
    float hori_player_speed;

    if (_gui_state.vis.IN_geo_vis_mode == _gui_state.vis.GLID_OF_CSGO_SESSION) {
        // World renderer needs server-side player position and velocity to
        // optimally visualize surface slidability
        player_feet_pos = _latest_csgo_server_data.player_pos_feet;
        hori_player_speed = _latest_csgo_server_data.player_vel.xy().length();
    }
    else {
        player_feet_pos = _cam_pos - Vector3(0.0f, 0.0f, CSGO_PLAYER_EYE_LEVEL_STANDING);
        hori_player_speed = _gui_state.vis.IN_specific_glid_vis_hori_speed;
    }


    if (_bsp_map) {
        GL::Renderer::enable(GL::Renderer::Feature::Blending);

        std::vector<Vector3> bump_mine_positions;
        bump_mine_positions.reserve(_latest_csgo_server_data.bump_mines.size());
        for (const auto& [id, bump_mine_data] : _latest_csgo_server_data.bump_mines) {
            bump_mine_positions.push_back(bump_mine_data.pos);
        }

        _world_renderer.Draw(
            _view_proj_transformation,
            player_feet_pos,
            hori_player_speed,
            bump_mine_positions);

        GL::Renderer::disable(GL::Renderer::Feature::Blending);
    }

    // Set appropriate states. If you only draw ImGui, it is sufficient to
    // just enable blending and scissor test in the constructor.
    GL::Renderer::enable(GL::Renderer::Feature::Blending);
    GL::Renderer::enable(GL::Renderer::Feature::ScissorTest);
    GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
    GL::Renderer::disable(GL::Renderer::Feature::FaceCulling);

    // Draw prominent horizontal velocity number
    if (_bsp_map && _gui_state.vis.IN_display_hori_vel_text) {
        switch (_gui_state.vis.IN_geo_vis_mode) {
            case _gui_state.vis.GLID_OF_CSGO_SESSION:
            case _gui_state.vis.GLID_AT_SPECIFIC_SPEED:
                float* imgui_col4 = _gui_state.vis.IN_col_hori_vel_text;
                Color4 c = { imgui_col4[0], imgui_col4[1], imgui_col4[2], imgui_col4[3]};
                _big_text_renderer.DrawNumber(
                    hori_player_speed,
                    c,
                    _gui.GetTotalGuiScaling() * _gui_state.vis.IN_hori_vel_text_size,
                    _gui_state.vis.IN_hori_vel_text_pos
                );
        }
    }

    // Show disclaimer when retrieving CSGO movement
    if(_csgo_rcon.IsConnected())
        _big_text_renderer.DrawDisclaimer(_gui.GetTotalGuiScaling());

    // Set up proper blending to be used by ImGui. There's a great chance you'll
    // need this exact behavior for the rest of your scene. If not, set this
    // only for the drawFrame() call.
    GL::Renderer::setBlendEquation(GL::Renderer::BlendEquation::Add,
        GL::Renderer::BlendEquation::Add);
    GL::Renderer::setBlendFunction(GL::Renderer::BlendFunction::SourceAlpha,
        GL::Renderer::BlendFunction::OneMinusSourceAlpha);

    _gui.Draw();

    // Reset state. Only needed if you want to draw something else with
    // different state after.
    GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);
    GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
    GL::Renderer::disable(GL::Renderer::Feature::ScissorTest);
    GL::Renderer::disable(GL::Renderer::Feature::Blending);

    swapBuffers();
}

MAGNUM_APPLICATION_MAIN(DZSimApplication)
