#include "gui/MenuWindow.h"

#include <cstdio>

#ifndef DZSIM_WEB_PORT
#include <Windows.h>
#include <shellapi.h>
#endif

#include <Corrade/Containers/Pair.h>
#include <Corrade/Containers/StringView.h>
#include <Corrade/Utility/Path.h>

#include "build_info.h"
#include "coll/Debugger.h"
#include "GlobalVars.h"
#include "gui/Gui.h"
#include "gui/GuiState.h"
#include "SavedUserDataHandler.h"

using namespace gui;
using namespace Corrade;

MenuWindow::MenuWindow(Gui& gui) : _gui(gui), _gui_state(gui.state)
{
#ifdef NDEBUG
    if (_gui_state.show_intro_msg_on_startup) {
        // In Release builds, show introductory message on startup
        ShowAppExplanation();
        // Remember to not show it again on next startup
        _gui_state.show_intro_msg_on_startup = false;
    }
#endif
}

void MenuWindow::Draw()
{
    ImGuiWindowFlags menu_window_flags = 0;
    //menu_window_flags |= ImGuiWindowFlags_NoTitleBar;
    menu_window_flags |= ImGuiWindowFlags_NoMove;
    menu_window_flags |= ImGuiWindowFlags_NoResize;
    menu_window_flags |= ImGuiWindowFlags_AlwaysAutoResize;

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();

    float menu_window_padding = _gui._total_gui_scaling * 10.0f;

    ImVec2 menu_pos = {
        main_viewport->WorkPos.x + 0.7f * menu_window_padding, // Looks better to me
        main_viewport->WorkPos.y + menu_window_padding
    };
    ImGui::SetNextWindowPos(menu_pos, ImGuiCond_Always);

    // Set max width or max height to -1 to disable the limits individually
    float max_menu_width = -1.0f; 
    float max_menu_height = main_viewport->WorkSize.y
        - 2 * menu_window_padding; // Top and bottom work area padding
    ImGui::SetNextWindowSizeConstraints({0, 0}, {max_menu_width, max_menu_height});

    ImGui::SetNextWindowBgAlpha(0.7f);

    // NULL for the bool* to remove close button
    if (!ImGui::Begin("Menu", NULL, menu_window_flags)) {
        ImGui::End(); // Early out if the window is collapsed, as an optimization
        return;
    }

    // Draw notification if update is available on GitHub
    if (_gui_state.OUT_dzsim_update_available) {
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.0f, 1.0f, 0.0f, 1.0f });
        ImGui::Text("A new version of DZSimulator is available for download!");
        ImGui::PopStyleColor(1);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.4f, 0.0f, 1.0f));
        if (ImGui::Button("Open downloads page in webbrowser"))
            _gui_state.IN_open_downloads_page_in_browser = true;
        ImGui::PopStyleColor(1);
        ImGui::Text("");
    }

    DrawMapSelection(); // MAP LOAD SELECTION

    const ImVec4 MENU_ELEM_COLOR = ImVec4(0.145f, 0.667f, 0.757f, 0.584f);
    ImGui::PushStyleColor(ImGuiCol_Header, MENU_ELEM_COLOR);

    if (ImGui::CollapsingHeader("First Person Controls"))
    {
        DrawFirstPersonControls();
    }

    if (ImGui::CollapsingHeader("Visualizations"))
    {
        DrawVisualizations();
    }

    if (ImGui::CollapsingHeader("Game Configuration"))
    {
        DrawGameConfig();
    }

    //if (ImGui::CollapsingHeader("CS:GO Integration"))
    //{
    //    // GSI is commented because it's currently pretty useless
    //    if (ImGui::TreeNode("GSI (inferior, but useful on any server as spectator)"))
    //    {
    //        ImGui::Checkbox("Enable GSI", &_gui_state.gsi.IN_enabled);
    //        if (!_gui_state.gsi.IN_enabled)
    //            ImGui::BeginDisabled();

    //        ImGui::Separator();
    //        ImGui::Checkbox("Imitate CS:GO spectator camera",
    //            &_gui_state.gsi.IN_imitate_spec_cam);
    //        ImGui::Separator();

    //        if (_gui_state.gsi.OUT_running)
    //            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "GSI running");
    //        else
    //            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "GSI not running");

    //        if (_gui_state.gsi.OUT_exited_unexpectedly)
    //            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
    //                "GSI server exited unexpectedly");

    //        if (ImGui::TreeNode("Latest game state data")) {
    //            ImGui::PushFont(_gui._font_mono); // Select monospace font
    //            ImGui::Text(_gui_state.gsi.OUT_info.c_str());
    //            ImGui::PopFont();
    //            if (ImGui::TreeNode("JSON payload")) {
    //                ImGui::PushFont(_gui._font_mono); // Select monospace font
    //                ImGui::Text(_gui_state.gsi.OUT_latest_json_payload.c_str());
    //                ImGui::PopFont();
    //                ImGui::TreePop();
    //            }
    //            ImGui::TreePop();
    //        }

    //        if (!_gui_state.gsi.IN_enabled)
    //            ImGui::EndDisabled();

    //        ImGui::TreePop();
    //    }
    //}

    if (ImGui::CollapsingHeader("Performance Stats"))
    {
        DrawPerformanceStats();
    }

    if (ImGui::CollapsingHeader("Video Settings"))
    {
        DrawVideoSettings();
    }

    if (sim::ENABLE_MOVEMENT_DEBUGGING) {
        if (ImGui::CollapsingHeader("Movement Debugging (Debug only)")) {
            DrawMovementDebugging();
        }
    }

#ifndef NDEBUG
    if (ImGui::CollapsingHeader("Collision Debugging (Debug only)"))
    {
        DrawCollisionDebugging();
    }

    if (ImGui::CollapsingHeader("Test Settings (Debug only)"))
    {
        DrawTestSettings();
    }
#endif

    if (ImGui::CollapsingHeader("About"))
    {
        if (ImGui::Button("What is this app? (Startup message)"))
            ShowAppExplanation();

        if (ImGui::Button("Why is this not a cheat and how does it work?"))
            ShowTechnicalities();

        if (ImGui::Button("How accurately are CS:GO mechanics recreated?"))
            ShowMovementRecreationDetails();

        if (ImGui::Button("Show known bugs"))
            ShowKnownIssues();

        if (ImGui::Button("Show new feature ideas"))
            ShowFeatureIdeas();

        ImGui::Text("");

        ImGui::Separator();

        ImGui::Text("\"Danger Zone Simulator\" version %s (%s)",
            build_info::GetVersionStr(),
            build_info::GetBuildTimeStr());

        // @PORTING: Make this work not just on Windows
        auto open_webpage = [](const char* url){
#ifdef DZSIM_WEB_PORT
            return; // TODO: Is opening a webpage possible on Emscripten?
#else
            // Open a website in the default browser (Windows-only)
            ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#endif
        };

        ImGui::Text("Made by lacyyy:  ");
        ImGui::SameLine(); if (ImGui::Button("GitHub"))
            open_webpage("https://github.com/lacyyy");
        ImGui::SameLine(); if (ImGui::Button("Twitter/X"))
            open_webpage("https://twitter.com/lacyyycs");
        ImGui::SameLine(); if (ImGui::Button("Twitch"))
            open_webpage("https://twitch.tv/lacyyycs");
        ImGui::SameLine(); if (ImGui::Button("Steam"))
            open_webpage("https://steamcommunity.com/profiles/76561198162669616");

        ImGui::Separator();
        ImGui::Text("");

        if (ImGui::TreeNode("Build information"))
        {
            ImGui::Text("- %s Build", build_info::GetBuildTypeStr());
            
#ifdef NDEBUG
            ImGui::Text("- NDEBUG defined");
#else
            ImGui::Text("- NDEBUG NOT defined");
#endif

            ImGui::Text("");

            ImGui::Text("Some of the used thirdparty libraries:");
            ImGui::PushFont(_gui._font_mono); // Select monospace font for build info
            ImGui::BulletText("Corrade            %s",
                build_info::thirdparty::GetCorradeVersionStr());
            ImGui::BulletText("Magnum             %s",
                build_info::thirdparty::GetMagnumVersionStr());
            ImGui::BulletText("Magnum Plugins     %s",
                build_info::thirdparty::GetMagnumPluginsVersionStr());
            ImGui::BulletText("Magnum Integration %s",
                build_info::thirdparty::GetMagnumIntegrationVersionStr());
#ifndef DZSIM_WEB_PORT
            ImGui::BulletText("SDL %s",
                build_info::thirdparty::GetSdlVersionStr());
#endif
            ImGui::BulletText("Dear ImGui %s",
                build_info::thirdparty::GetImGuiVersionStr());
            ImGui::BulletText("Asio %s",
                build_info::thirdparty::GetAsioVersionStr());
#ifndef DZSIM_WEB_PORT
            ImGui::BulletText("OpenSSL %s",
                build_info::thirdparty::GetOpenSSLVersionStr());
#endif
            ImGui::BulletText("cpp-httplib %s",
                build_info::thirdparty::GetCppHttpLibVersionStr());
            ImGui::BulletText("nlohmann/json %s",
                build_info::thirdparty::GetJsonVersionStr());
            ImGui::BulletText("podgorskiy/fsal %s",
                build_info::thirdparty::GetFsalVersionStr());

            ImGui::PopFont();

            ImGui::TreePop();
        }

        if (ImGui::Button("Show third party legal notices"))
            _gui_state.show_window_legal_notices ^= true;

        { // Settings reset button. Upon pressing, ask user to confirm it once more
            static bool s_confirming_reset = false;
            if (!s_confirming_reset) {
                if (ImGui::Button("Reset all settings"))
                    s_confirming_reset = true;
            }
            else {
                if (ImGui::Button("   YES   ##reset"))
                {
                    // === RESET ALL SETTINGS ===

                    // Some settings shouldn't be reset.
                    // (To avoid bugs or annoyances to the user.)
                    auto temp1 = _gui_state.show_intro_msg_on_startup;
                    auto temp2 = _gui_state.video.IN_window_mode;
                    auto temp3 = _gui_state.video.IN_selected_display_idx;

                    SavedUserDataHandler::ResetUserSettingsToDefaults(_gui_state);

                    // Restore some settings again
                    _gui_state.show_intro_msg_on_startup     = temp1;
                    _gui_state.video.IN_window_mode          = temp2;
                    _gui_state.video.IN_selected_display_idx = temp3;

                    // Trigger some updates
                    _gui._gui_scaling_update_required = true;

                    s_confirming_reset = false;
                }
                ImGui::SameLine(); if (ImGui::Button("   NO   ##reset")) s_confirming_reset = false;
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 0.55f, 0.0f, 1.0f });
                ImGui::Text("Are you sure you want to reset everything? Can't undo!");
                ImGui::PopStyleColor(1); // ImGuiCol_Text
            }
        }
        ImGui::Separator();
        ImGui::Separator();
    }

    ImGui::Text("");
    ImGui::PushStyleColor(ImGuiCol_Button, MENU_ELEM_COLOR);
    if (ImGui::Button(" QUIT ")) _gui_state.app_exit_requested = true;
    ImGui::PopStyleColor(1); // ImGuiCol_Button

    ImGui::PopStyleColor(1); // ImGuiCol_Header

    ImGui::End();
}

void MenuWindow::ShowAppExplanation()
{
    _gui_state.popup.QueueMsgInfo(
        "This is the \"Danger Zone Simulator\", a tool for practicing "
        "\"Bump Mine\" jumps in the battle-royale game mode \"Danger Zone\""
        " of Counter-Strike: Global Offensive.\n\n"
        "Due to bugs in the video game engine, players can satisfyingly "
        "slide up sloped surfaces if they have enough speed. (Check out "
        "\"Speed in Excess\" by \"catfjsh?\" on YouTube: "
        "https://youtu.be/xmAYeyYd4AE )\n\n"
        "One of the difficulties of that playstyle is figuring out which "
        "parts of surfaces are suitable for rampsliding. That's what this "
        "app tries to make easy.\n\n"
        "NOTE: This is NOT a CS:GO cheat and can't get your account VAC-banned."
        " The CS:GO-movement-tracking feature only works on servers where "
        "\"sv_cheats\" can be set to 1. For technical details, see the "
        "\"About\" section in the menu.\n\n"
        "You must have CS:GO installed, because this app works by simply "
        "loading maps from CS:GO's game files!\n\n"
        "Even when a surface is considered glidable, you might still fail to "
        "achieve the rampslide in CS:GO because that mechanic is inherently "
        "random! It seems like players can do nothing about that fact.\n\n"
        "If you find any bugs, please report them in the \"Issues\" tab on "
        "the project's website (https://github.com/lacyyy/DZSimulator), "
        "where you should have downloaded this app from.\nYou can also send "
        "me bug reports and feedback through my Twitter/X DMs: "
        "https://twitter.com/lacyyycs"
    );
}

void MenuWindow::ShowTechnicalities()
{
    _gui_state.popup.QueueMsgInfo(
        "This program is NOT a CS:GO cheat and can't get your account "
        "VAC-banned.\n\n"
        "Instead, it is a standalone graphics application that's able to load "
        "CS:GO map elements that are relevant to rampsliding (and more) and "
        "show the 3D world on screen.\n\n"
        "It has one feature that seems to be a cheat, but isn't: The ability "
        "to copy the player's movement from within a CS:GO game and then "
        "show the world in DZSimulator from that player's point of view. "
        "(ONLY WORKS IN OFFLINE MATCHES)\n\n"
        "That's very useful as it allows for a transparent overlay on top of "
        "CS:GO with helpful rampsliding information.\n\n"
        "This is possible through a feature built into CS:GO : The "
        "\"netconport\" launch option. With it, DZSimulator can connect to "
        "CS:GO's console, try to run \"sv_cheats 1\" and if that was allowed, "
        "get player movement info through \"getpos\" and other cheat-protected "
        "commands.\n\n"
        "As you can see, that's completely legit and requires the permission "
        "to set the server's ConVar \"sv_cheats\" to 1, therefore not being "
        "possible and exploitable in online matches!"
    );
}

void MenuWindow::ShowMovementRecreationDetails()
{
    _gui_state.popup.QueueMsgInfo(
        "While this app tries to recreate game mechanics as accurately as "
        "possible, there are some differences to CS:GO:\n\n"
        "  - No water movement\n"
        "  - No ladder climbing\n"
        "  - No working push triggers\n"
        "  - Wrong friction and acceleration on some surfaces\n"
        "  - Forwards exojump boosts are always perfect (as if the player "
        "crouched and jumped at the exact same time)\n"
        "  - Getting stuck in displacements behaves differently\n"
        "  - No accelerated exojump back hopping\n"
        "  - No stamina reductions (e.g. from jumping or landing)\n"
        "  - No crouch spamming penalty\n"
        "  - Inaccurate eye position during crouching/uncrouching\n"
        "  - Walking and rampsliding on props can be slightly inaccurate (up "
        "to 1 unit off)"
    );
}

void MenuWindow::ShowKnownIssues()
{
    _gui_state.popup.QueueMsgWarn(
        "Known issues that might be addressed in the future:\n\n"
        "  - Some transparent objects disappear when looking through certain "
        "other transparent objects\n"
        "  - Some objects flicker a lot, especially in the visualization mode "
        "'Geometry type'\n"
    );
}

void MenuWindow::ShowFeatureIdeas()
{
    _gui_state.popup.QueueMsgInfo(
        "Some ideas for features I had (No promises they'll happen!):\n\n"
        "  - Option to rebind input keys (yes I know this is kind of needed)\n"
        "  - Show Bump Mine's arming process/progress\n"
        "  - Show Bump Mine's activation area\n"
        "  - Show player's predicted trajectory/impact point\n"
        "  - Show Bump Mine's predicted trajectory/impact point\n"
        "  - Improve algorithm for coloring glidable surfaces\n"
        "  - Rewind time! Skip your jump back a few seconds to continue from "
        "earlier again\n"
        "  - Slow down time\n"
        "  - Show player how to optimally strafe at their current speed\n"
        "  - Add textures to surfaces\n"
        "  - Maybe (maybe maybe) loading Counter-Strike 2 maps"
    );
}

void MenuWindow::ShowOverlayLagAdvice()
{
    _gui_state.popup.QueueMsgInfo(
        "When you use DZSimulator as an overlay on top of CS:GO, you might "
        "encounter a noticable visual delay between CS:GO and the overlay, on "
        "some maps worse than on others. Try reducing it with these steps:\n\n"

        "1. Reduce CS:GO's FPS limit (preferably to 128 or 64). As you decrease "
        "it, the overlay should get smoother. For example, you can set the FPS "
        "limit to 64 by entering this into CS:GO's console:\n\n"
        "    fps_max 64\n\n"

        "2. If step 1 didn't help enough, make sure your local server is "
        "running at a tick rate of 64. It's likely your machine lacks the "
        "power to smoothly run a local server (especially with a DZ map) on a "
        "tick rate of 128.\n"
        "To make sure you're on 64, Go to your Steam library, right-click "
        "CS:GO, go to \"Properties\" > \"General\" > \"Launch Options\". "
        "There, remove any \"-tickrate XXX\" option and restart CS:GO. If you "
        "don't have it, your tick rate is already at 64.\n\n"

        "3. If your machine is powerful enough to smoothly run a high tick "
        "rate local DZ server (this can depend on the map), switching to a tick"
        "rate of 128 can actually make the overlay smoother! To do that, add "
        "the launch option \"-tickrate 128\" and restart CS:GO.\n\n"

        "4. If the overlay is still too laggy, try some other maps that might "
        "give a smoother overlay, sorry!"
    );
}

void MenuWindow::DrawMapSelection()
{
    static std::string s_map_load_box_preview = "SELECT MAP TO LOAD";
    static bool s_prev_is_map_load_box_open = false;
    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.5f, 1.0f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.5f, 0.8f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0.5f, 1.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, (ImVec4)ImColor::HSV(0.5f, 0.8f, 0.4f));
    bool is_map_load_box_open = false;
    if (ImGui::BeginCombo("##MapLoadComboBox", s_map_load_box_preview.c_str(), 0)) {
        is_map_load_box_open = true;

        size_t highlighted = _gui_state.map_select.OUT_num_highlighted_maps;
        for (const std::string& rel_map_path : _gui_state.map_select.OUT_loadable_maps) {
            if (highlighted)
                ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(1.0f, 1.0f, 0.8f));

            if (ImGui::Selectable(rel_map_path.c_str(), false)) {
                _gui_state.map_select.IN_new_abs_map_path_load =
                    Corrade::Utility::Path::join(
                        { _gui_state.map_select.OUT_csgo_path, "../csco/csgo/maps/", rel_map_path });
                s_map_load_box_preview = rel_map_path;
            }
            if (highlighted) {
                ImGui::PopStyleColor(1);
                highlighted--;
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(0.2f, 1.0f, 1.0f));
        // Last item opens file dialog to choose path manually
        if (ImGui::Selectable("< SELECT MAP FILE FROM DISK >", false)) {
            std::string fd_path = _gui.OpenBspFileDialog();
            if (!fd_path.empty()) {
                Containers::StringView file_name =
                    Corrade::Utility::Path::split(fd_path).second();
                if (!file_name.isEmpty()) {
                    _gui_state.map_select.IN_new_abs_map_path_load = fd_path;
                    s_map_load_box_preview = "<...>/" + file_name;
                }
            }
        }
        ImGui::PopStyleColor(1);
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(4);

    // If map load box is open this frame and was not open last frame
    if (is_map_load_box_open && !s_prev_is_map_load_box_open)
        _gui_state.map_select.IN_box_opened = true; // -> user just opened box
    s_prev_is_map_load_box_open = is_map_load_box_open; // save for next frame
}

void MenuWindow::DrawFirstPersonControls()
{
#define CTRL_TABLE_ROW(cell1, cell2) do { \
        ImGui::TableNextColumn(); ImGui::Text((cell1)); \
        ImGui::TableNextColumn(); ImGui::Text((cell2)); \
    } while(0)

    if (ImGui::BeginTable("ctrls_table", 2, ImGuiTableFlags_BordersInner))
    {
        CTRL_TABLE_ROW("ESC",                 "Toggle first person control");
        ImGui::SameLine(); _gui.HelpMarker(
            "Press the ESC key to start moving in the loaded map.\n"
            "Press the ESC key again to navigate the menu.\n"
            "All of the following controls only work in first person control mode!");
        CTRL_TABLE_ROW("W",                   "Move forwards");
        CTRL_TABLE_ROW("A",                   "Move left");
        CTRL_TABLE_ROW("S",                   "Move backwards");
        CTRL_TABLE_ROW("D",                   "Move right");
        CTRL_TABLE_ROW("Space",               "Jump");
        CTRL_TABLE_ROW("Scroll Wheel",        "Jump");
        CTRL_TABLE_ROW("Left Shift",          "Walk");
        CTRL_TABLE_ROW("Left Ctrl",           "Crouch");
        CTRL_TABLE_ROW("Left Mouse Button",   "Throw Bump Mine");
        CTRL_TABLE_ROW("Middle Mouse Button", "Save current state of the world");
        ImGui::SameLine(); _gui.HelpMarker(
            "This remembers your current position and speed,\n"
            "as well as that of every Bump Mine on the map!\n"
            "Later you can recreate this exact state of the world.\n"
            "Useful for practicing the same jump over and over.");
        CTRL_TABLE_ROW("Right Mouse Button",  "Load state of the world that was saved earlier");
        CTRL_TABLE_ROW("F",                   "Toggle noclip/flying mode");
        CTRL_TABLE_ROW("C",                   "Clear all Bump Mines from the map");
        ImGui::EndTable();
    }

    ImGui::Text("");

    ImGui::SliderFloat("Mouse Sensitivity", &_gui_state.ctrls.IN_mouse_sensitivity,
                       0.001f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> This sensitivity is identical to CS:GO's and CS2's sensitivity.");
}

void MenuWindow::DrawVisualizations()
{
    ImGui::Text("Click on the color square to open a color picker.\n"
                "CTRL+click on an individual component to input a value.");

    auto& cols = _gui_state.vis;
    ImGuiColorEditFlags picker_flags =
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_AlphaPreviewHalf |
        ImGuiColorEditFlags_Float |
        ImGuiColorEditFlags_NoDragDrop |
        ImGuiColorEditFlags_PickerHueWheel;

    ImGui::ColorEdit3("Bump Mine Color",
                      (float*)&cols.IN_col_bump_mine, picker_flags);
    ImGui::ColorEdit3("Sky Color",
                      (float*)&cols.IN_col_sky, picker_flags);
    ImGui::ColorEdit3("Ladder Color",
                      (float*)&cols.IN_col_ladders, picker_flags);
    ImGui::ColorEdit4("Push Trigger Color",
                      (float*)&cols.IN_col_trigger_push, picker_flags);
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> This settings sets the color of all trigger_push entities "
        "that can\n"
        "push players. Some always push players, some only when you "
        "fall into\n"
        "them while NOT pressing jump!");
    ImGui::ColorEdit4("Water Color",
                      (float*)&cols.IN_col_water, picker_flags);
    ImGui::ColorEdit4("Grenade Clip Color",
                      (float*)&cols.IN_col_grenade_clip, picker_flags);
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> Grenade clips are solid to thrown Bump Mines, but not to players!\n"
        "They are rarely present in Danger Zone maps though.");
    ImGui::ColorEdit4("Player Clip Color",
        (float*)&cols.IN_col_player_clip, picker_flags);
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> Player clips are solid to players, but not to thrown Bump Mines!");

    // Light angle
    ImGui::SliderFloat("Sunlight Direction",
                       &_gui_state.vis.IN_hori_light_angle, 0.0f, 360.0f, "%.1f");

    // Displacement edges
    ImGui::Checkbox("Show Displacement Edges", &_gui_state.vis.IN_draw_displacement_edges);
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> Many hilly surfaces and sometimes even roofs and walls are made of displacements.\n"
        "They come in different sizes and connect seamlessly to each other.\n"
        "The problem with them: Players that rampslide over their boundary edges can\n"
        "easily collide with them, making it appear like the player hit a wall.  :(\n"
        "By knowing where these dangerous edges are, you might be able to avoid them.\n"
    );
    if (_gui_state.vis.IN_draw_displacement_edges) {
        ImGui::ColorEdit3("Displacement Edge Color",
                          (float*)&cols.IN_col_solid_disp_boundary, picker_flags);
    }

    ImGui::Text("");
    ImGui::Separator();

    // Horizontal player velocity text
    ImGui::Checkbox("Show Horizontal Speed Display",
                    &_gui_state.vis.IN_display_hori_vel_text);
    if (_gui_state.vis.IN_display_hori_vel_text) {
        ImGui::SliderFloat("Speed Display Size",
                           &_gui_state.vis.IN_hori_vel_text_size, 0.1f, 4.0f, "%.1f");
        ImGui::ColorEdit3("Speed Display Color",
                          (float*)&cols.IN_col_hori_vel_text, picker_flags);
        ImGui::SliderFloat("Speed Display X Position",
                           &_gui_state.vis.IN_hori_vel_text_pos.x(), -0.5f, 0.5f, "%.3f");
        ImGui::SliderFloat("Speed Display Y Position",
                           &_gui_state.vis.IN_hori_vel_text_pos.y(), -0.5f, 0.5f, "%.3f");
    }

    ImGui::Text("");
    ImGui::Separator();

    // Crosshair
    ImGui::ColorEdit4("Crosshair Color", (float*)&cols.IN_crosshair_col, picker_flags);
    ImGui::SameLine(); _gui.HelpMarker(">>>> Set alpha to 0 to hide the crosshair.");
    ImGui::SliderFloat("Crosshair Size",      &_gui_state.vis.IN_crosshair_scale,
                       0.1f, 10.0f, "%.2f",  ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Crosshair Length",    &_gui_state.vis.IN_crosshair_length,
                       0.5f, 150.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Crosshair Thickness", &_gui_state.vis.IN_crosshair_thickness,
                       0.5f, 20.0f, "%.1f",  ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Crosshair Gap",       &_gui_state.vis.IN_crosshair_gap,
                       0.0f, 200.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

    ImGui::Text("");
    ImGui::Separator();

    ImGui::Text("Geometry Visualization Mode:");

    auto& geo_vis_mode = _gui_state.vis.IN_geo_vis_mode;

    if (ImGui::RadioButton("Glidability for player in simulation",
                           geo_vis_mode == _gui_state.vis.GLID_OF_SIMULATION))
        geo_vis_mode = _gui_state.vis.GLID_OF_SIMULATION;
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> This mode shows how glidable surfaces are depending on the\n"
        "player movement inside DZSimulator.\n"
        "Surface glidability is determined assuming that the player moves\n"
        "towards each surface with their current horizontal speed.");

    if (ImGui::RadioButton("Glidability at specific player speed",
                           geo_vis_mode == _gui_state.vis.GLID_AT_SPECIFIC_SPEED))
        geo_vis_mode = _gui_state.vis.GLID_AT_SPECIFIC_SPEED;
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> Show how glidable every surface is under the assumption that\n"
        "the player moves directly towards that surface with the given\n"
        "horizontal speed.");

    if (ImGui::RadioButton("Glidability for player in local CS:GO session",
                           geo_vis_mode == _gui_state.vis.GLID_OF_CSGO_SESSION))
        geo_vis_mode = _gui_state.vis.GLID_OF_CSGO_SESSION;
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> This mode uses the player's position and speed from a local\n"
        "CS:GO session. It shows how glidable surfaces are under the assumption\n"
        "that the player moves towards each surface with the current horizontal\n"
        "speed of the player.");

    if (ImGui::RadioButton("Geometry type",
                           geo_vis_mode == _gui_state.vis.GEO_TYPE))
        geo_vis_mode = _gui_state.vis.GEO_TYPE;
    ImGui::SameLine(); _gui.HelpMarker(
        ">>>> Draws surfaces with different colors depending on their object's type.");

    ImGui::Text("");
    ImGui::Separator();

    // GLID_* mode settings
    if (geo_vis_mode == _gui_state.vis.GLID_OF_SIMULATION
        || geo_vis_mode == _gui_state.vis.GLID_AT_SPECIFIC_SPEED
        || geo_vis_mode == _gui_state.vis.GLID_OF_CSGO_SESSION) {

        // Surface slide colors
        ImGui::ColorEdit3("Slide Success Color",
                          (float*)&cols.IN_col_slide_success, picker_flags);
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Under current conditions, rampsliding is possible on\n"
            "surfaces with this color.");
        ImGui::ColorEdit3("Slide Almost-Fail Color",
                          (float*)&cols.IN_col_slide_almost_fail, picker_flags);
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Under current conditions, rampsliding is possible on\n"
            "surfaces with this color, but the slightest change in speed\n"
            "and impact angle might cause you to fail the rampslide.");
        ImGui::ColorEdit3("Slide Fail Color",
                          (float*)&cols.IN_col_slide_fail, picker_flags);
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Under current conditions, rampsliding isn't possible\n"
            "on surfaces with this color.");

        ImGui::Text("");
        ImGui::Separator();
    }

    // GLID_AT_SPECIFIC_SPEED vis mode settings
    if (geo_vis_mode == _gui_state.vis.GLID_AT_SPECIFIC_SPEED) {
        ImGui::SliderInt("Specific Horizontal Speed",
                         &_gui_state.vis.IN_specific_glid_vis_hori_speed, 100, 5000, "%d");
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Depending on the player's speed, surfaces change their glidability.\n"
            "Enter the player's horizontal speed to see glidable surfaces with it.\n"
            "Don't know the value? In CS:GO, run \"cl_showpos 1\" in an offline game\n"
            "and read the \"vel\" value in the top left screen corner. That's the\n"
            "current in-game horizontal player speed.");
        if (_gui_state.vis.IN_specific_glid_vis_hori_speed < 1) // Avoid division by 0
            _gui_state.vis.IN_specific_glid_vis_hori_speed = 1;
    }
        // GLID_OF_CSGO_SESSION vis mode settings
    else if (geo_vis_mode == _gui_state.vis.GLID_OF_CSGO_SESSION) {
        ImGui::Text(
            "This visualization mode only works if you connect to a local CS:GO\n"
            "session that has the same map loaded and was started with the\n"
            "launch option:   -netconport 34755");

        bool connect_allowed =
            (!_gui_state.rcon.OUT_is_connecting && !_gui_state.rcon.OUT_is_connected)
            || _gui_state.rcon.OUT_is_disconnecting;
        bool disconnect_allowed = !connect_allowed;
        // ----
        if (!connect_allowed) ImGui::BeginDisabled();
        if (ImGui::Button("CONNECT"))
            _gui_state.rcon.IN_start_connect = true;
        if (!connect_allowed) ImGui::EndDisabled();
        // ----
        if (!disconnect_allowed) ImGui::BeginDisabled();
        ImGui::SameLine();
        if (ImGui::Button("DISCONNECT"))
            _gui_state.rcon.IN_disconnect = true;
        if (!disconnect_allowed) ImGui::EndDisabled();
        // ----
        ImGui::SameLine();
        if (_gui_state.rcon.OUT_is_connecting)
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                               "Connecting...");
        else if (_gui_state.rcon.OUT_is_disconnecting)
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f),
                               "Disconnecting...");
        else if (_gui_state.rcon.OUT_is_connected)
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                               "Connected!");
        else if (_gui_state.rcon.OUT_has_connect_failed)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "Failed to connect!");
        else if (!_gui_state.rcon.OUT_is_connected)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "Not connected!");
        // ----
        if (_gui_state.rcon.OUT_fail_msg.length() > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "%s", _gui_state.rcon.OUT_fail_msg.c_str());

        ImGui::Text("");

#ifndef DZSIM_WEB_PORT
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f,0.86f,0.46f,0.4f));
        if (ImGui::Button("How to fix lag when used as CS:GO overlay"))
            ShowOverlayLagAdvice();
        ImGui::PopStyleColor(1);
#endif
    }
        // GEO_TYPE vis mode settings
    else if (geo_vis_mode == _gui_state.vis.GEO_TYPE) {
        ImGui::Text("Further color settings:");
        ImGui::ColorEdit3("Solid Displacement Color",
                          (float*)&cols.IN_col_solid_displacements, picker_flags);
        ImGui::ColorEdit3("Solid Prop Color",
                          (float*)&cols.IN_col_solid_xprops, picker_flags);
        ImGui::ColorEdit3("Other Solid Brush Color",
                          (float*)&cols.IN_col_solid_other_brushes, picker_flags);
    }
}

void MenuWindow::DrawGameConfig()
{
    using GameMode = sim::CsgoConfig::GameMode;
    using Weapon   = sim::Entities::Player::Loadout::Weapon;
    GuiState::GameConfiguration&    cfg     = _gui_state.game_cfg;
    sim::Entities::Player::Loadout& loadout = _gui_state.game_cfg.IN_loadout;

    ImGui::Text("Simulated game mode:");
    ImGui::SameLine();
    _gui.HelpMarker(
        ">>>> Some visualizations and movement mechanics behave differently\n"
        "depending on the simulated game mode (e.g. player running speeds).");

    bool dz_active = cfg.IN_game_mode == GameMode::DANGER_ZONE;
    if (ImGui::RadioButton("CS:GO Danger Zone", dz_active))
        _gui_state.game_cfg.IN_game_mode = GameMode::DANGER_ZONE;

    ImGui::SameLine();

    bool comp_active = cfg.IN_game_mode == GameMode::COMPETITIVE;
    if (ImGui::RadioButton("CS:GO Competitive", comp_active))
        _gui_state.game_cfg.IN_game_mode = GameMode::COMPETITIVE;

    ImGui::Separator();

    ImGui::Checkbox("Enable fix for consistent Bump Mine activations",
        &_gui_state.game_cfg.IN_enable_consistent_bumpmine_activations);
    ImGui::SameLine();
    _gui.HelpMarker(
        ">>>> By default in CS:GO, Bump Mines check for player activations only\n"
        "10 times per second. This leads to players sometimes failing to activate\n"
        "them at higher moving speeds.\n"
        "Enable this fix to let Bump Mines check for player activations every tick.");

    ImGui::Checkbox("Enable fix for consistent rampslides",
                    &_gui_state.game_cfg.IN_enable_consistent_rampslides);
    ImGui::SameLine();
    _gui.HelpMarker(
        ">>>> By default in CS:GO, roughly 1 in 10 rampslide attempts (on 64 tick)\n"
        "randomly fail even when the player has the right speed and impact angle.\n"
        "Enable this fix to eliminate this source of randomness.\n"
        "Note that you might still get randomly stuck on uneven surfaces while\n"
        "rampsliding.");

    ImGui::Separator();

    ImGui::Text("Simulated player equipment:");
    ImGui::Indent();

    ImGui::Checkbox("Exojump", &loadout.has_exojump);

    ImGui::Text("Held weapon:");
    ImGui::SameLine();
    const char* combo_box_preview;
    switch (loadout.active_weapon) {
        case Weapon::Knife:    combo_box_preview = "Knife";     break;
        case Weapon::BumpMine: combo_box_preview = "Bump Mine"; break;
        case Weapon::Fists:    combo_box_preview = "Fists";     break;
        case Weapon::Taser:    combo_box_preview = "Zeus x27";  break;
        case Weapon::XM1014:   combo_box_preview = "XM1014";    break;
        default:               combo_box_preview = "";          break;
    }
    if (ImGui::BeginCombo("##ActiveWeaponCombo", combo_box_preview, ImGuiComboFlags_WidthFitPreview)) {
        if (ImGui::Selectable("Knife##active",     loadout.active_weapon == Weapon::Knife   )) loadout.active_weapon = Weapon::Knife;
        if (ImGui::Selectable("Bump Mine##active", loadout.active_weapon == Weapon::BumpMine)) loadout.active_weapon = Weapon::BumpMine;
        if (ImGui::Selectable("Fists##active",     loadout.active_weapon == Weapon::Fists   )) loadout.active_weapon = Weapon::Fists;
        if (ImGui::Selectable("Zeus x27##active",  loadout.active_weapon == Weapon::Taser   )) loadout.active_weapon = Weapon::Taser;
        if (ImGui::Selectable("XM1014##active",    loadout.active_weapon == Weapon::XM1014  )) loadout.active_weapon = Weapon::XM1014;
        ImGui::EndCombo();
    }
    // Ensure that the active weapon isn't in the non-active weapon list
    loadout.non_active_weapons.set(loadout.active_weapon, false);

    ImGui::Text("Other carried weapons:"); ImGui::Indent(); {
        bool is_non_active[Weapon::TOTAL_COUNT];
        for (size_t i = 0; i < Weapon::TOTAL_COUNT; i++)
            is_non_active[i] = loadout.non_active_weapons[i];

        if (loadout.active_weapon == Weapon::Knife) ImGui::BeginDisabled();
        ImGui::Checkbox("Knife##nonactive", &is_non_active[Weapon::Knife]);
        if (loadout.active_weapon == Weapon::Knife) ImGui::EndDisabled();

        if (loadout.active_weapon == Weapon::BumpMine) ImGui::BeginDisabled();
        ImGui::Checkbox("Bump Mine##nonactive", &is_non_active[Weapon::BumpMine]);
        if (loadout.active_weapon == Weapon::BumpMine) ImGui::EndDisabled();

        if (loadout.active_weapon == Weapon::Fists) ImGui::BeginDisabled();
        ImGui::Checkbox("Fists##nonactive", &is_non_active[Weapon::Fists]);
        if (loadout.active_weapon == Weapon::Fists) ImGui::EndDisabled();

        if (loadout.active_weapon == Weapon::Taser) ImGui::BeginDisabled();
        ImGui::Checkbox("Zeus x27##nonactive", &is_non_active[Weapon::Taser]);
        if (loadout.active_weapon == Weapon::Taser) ImGui::EndDisabled();

        if (loadout.active_weapon == Weapon::XM1014) ImGui::BeginDisabled();
        ImGui::Checkbox("XM1014##nonactive", &is_non_active[Weapon::XM1014]);
        if (loadout.active_weapon == Weapon::XM1014) ImGui::EndDisabled();

        for (size_t i = 0; i < Weapon::TOTAL_COUNT; i++)
            loadout.non_active_weapons.set(i, is_non_active[i]);
    }
    ImGui::Unindent();

    ImGui::Unindent();

    ImGui::Separator();

    float loadout_max_running_speed =
        g_csgo_game_sim_cfg.GetMaxPlayerRunningSpeed(loadout);
    ImGui::Text("Resulting max running speed: %.1f", loadout_max_running_speed);
}

void MenuWindow::DrawPerformanceStats()
{
    //ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
    //    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::Text("Average FPS: %.1f", 1000.0f / _gui_state.perf.OUT_frame_time_mean_ms);

    ImGui::Text("%s", _gui_state.perf.OUT_magnum_profiler_stats.cbegin());

    ImGui::Separator();

    ImGui::Text("Game sim calculation time:  %.1f us",
                _gui_state.perf.OUT_last_sim_calc_time_us);

}

void MenuWindow::DrawVideoSettings()
{
    auto& win_mode = _gui_state.video.IN_window_mode;

    { // FOV setting
        ImGui::Checkbox("Use a custom FOV value", &_gui_state.video.IN_use_custom_fov);
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Enable this to increase or decrease DZSimulator's Field of "
            "View (FOV).");

        if (!_gui_state.video.IN_use_custom_fov)
            ImGui::BeginDisabled();

        ImGui::SliderFloat("Custom Vertical FOV",
            &_gui_state.video.IN_custom_vert_fov_degrees, 5.0f, 170.0f, "%.1f");
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> By default, CS:GO's vertical FOV is fixed to 73.7 degrees.\n"
            "Note: The values of CS:GO's console command \"fov_cs_debug\" do\n"
            "not correspond to their corresponding vertical FOV value!");

        if (!_gui_state.video.IN_use_custom_fov)
            ImGui::EndDisabled();
    }

    ImGui::Text("");

    { // VSync setting
        ImGui::Checkbox("Enable VSync", &_gui_state.video.IN_vsync_enabled);
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> VSync fixes the maximum FPS to your monitor's refresh rate.\n"
            "You can't choose a custom FPS limit while VSync is enabled.\n"
            "If you are having trouble with input lag or stuttering, "
            "disable VSync.\n"
            "If you are having trouble with screen tearing, enable VSync.");
    }

    if (_gui_state.video.IN_vsync_enabled)
        ImGui::BeginDisabled();

    { // FPS limit setting
        int cur_min_loop_period = _gui_state.video.IN_min_loop_period;

        const int MIN_FPS_LIMIT = 10;
        const int MAX_FPS_LIMIT = 500;

        const size_t SLIDER_VAL_COUNT = // FPS vals + Unlimited val
            (MAX_FPS_LIMIT - MIN_FPS_LIMIT + 1) + 1;

        static int slider_val = cur_min_loop_period == 0 ?
            SLIDER_VAL_COUNT - 1 :
            (1000 / cur_min_loop_period) - MIN_FPS_LIMIT;
        const char* slider_text = "";
        char t_buf[64];
        bool fps_unlimited = false;
        if (slider_val >= 0 && slider_val < SLIDER_VAL_COUNT - 1) {
            unsigned int selected_max_fps = slider_val + MIN_FPS_LIMIT;
            unsigned int min_loop_period_ms = 1000 / selected_max_fps;
            if (min_loop_period_ms == 0) {
                fps_unlimited = true;
            }
            else {
                _gui_state.video.IN_min_loop_period = min_loop_period_ms;
                int real_max_fps = 1000 / min_loop_period_ms;
                std::snprintf(t_buf, 64, "%d FPS", real_max_fps);
                slider_text = t_buf;
            }
        }
        else {
            fps_unlimited = true;
        }

        if (fps_unlimited) {
            _gui_state.video.IN_min_loop_period = 0;
            slider_text = "No limit (GPU intensive)";
        }

        ImGui::SliderInt("FPS Limit", &slider_val, 0, SLIDER_VAL_COUNT - 1,
            slider_text, ImGuiSliderFlags_NoInput);
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Set an FPS limit to reduce (or increase) the load on your computer.\n"
            "Dragging the slider as far right as possible disables the FPS limit.\n"
            "High FPS limits are imprecise because of technical limitations.");
    }

    if (_gui_state.video.IN_vsync_enabled)
        ImGui::EndDisabled();

    ImGui::Text("");

    { // Window mode setting
        const char* mode_name__windowed = "Windowed";
        const char* mode_name__fullscreen_windowed = "Fullscreen Windowed";

        const char* preview = "UNKNOWN MODE";
        switch (win_mode) {
        case GuiState::VideoSettings::WINDOWED:
            preview = mode_name__windowed; break;
        case GuiState::VideoSettings::FULLSCREEN_WINDOWED:
            preview = mode_name__fullscreen_windowed; break;
        }

        if (ImGui::BeginCombo("Display Mode ( F11 )", preview, 0)) {
            if (ImGui::Selectable(mode_name__windowed, false)) {
                win_mode = GuiState::VideoSettings::WINDOWED;
            }
            if (ImGui::Selectable(mode_name__fullscreen_windowed, false)) {
                win_mode = GuiState::VideoSettings::FULLSCREEN_WINDOWED;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> You can toggle display modes by pressing F11");
    }

    { // Display selection (FULLSCREEN_WINDOWED mode only)
        auto& displays = _gui_state.video.OUT_available_displays;
        auto& selected_display_idx = _gui_state.video.IN_selected_display_idx;

        // We recommend using the first display because other displays might not
        // work in "Fullscreen Windowed" with HiDPI settings that are different
        // from that of the first display. More detailed explanation should be
        // at the main window fullscreen handling code.
        // This display recommendation relies on the assumption that
        // SDL_GetDisplayBounds() succeeded with display index 0 .
        const char* recommended_suffix = " <<< RECOMMENDED";

        if (win_mode != GuiState::VideoSettings::FULLSCREEN_WINDOWED)
            ImGui::BeginDisabled();

        static std::string s_preview;
        if (selected_display_idx >= 0 && selected_display_idx < displays.size()) {
            s_preview = GetDisplayName(selected_display_idx + 1,
                displays[selected_display_idx].w,
                displays[selected_display_idx].h);
            if (selected_display_idx == 0)
                s_preview += recommended_suffix;
        }
        else
            s_preview = "No display selected";

        static bool s_prev_is_disp_selection_open = false;
        bool is_disp_selection_open = false;
        if (ImGui::BeginCombo("Display Selection", s_preview.c_str(), 0)) {
            is_disp_selection_open = true;

            for (size_t i = 0; i < displays.size(); i++) {
                std::string display_name =
                    GetDisplayName(i + 1, displays[i].w, displays[i].h);
                if (i == 0)
                    display_name += recommended_suffix;

                if (ImGui::Selectable(display_name.c_str(), false))
                    _gui_state.video.IN_selected_display_idx = i;
            }
            ImGui::EndCombo();
        }
        // If user has just opened the display selection box, send signal to
        // game code to refresh display info for next frame
        if (is_disp_selection_open && !s_prev_is_disp_selection_open)
            _gui_state.video.IN_available_display_refresh_needed = true;
        // Remember for next frame
        s_prev_is_disp_selection_open = is_disp_selection_open;

        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Select which display to use for \"Fullscreen Windowed\" mode.\n\n"
            "CAUTION: In uncommon conditions, \"Fullscreen Windowed\" doesn't work.\n"
            "Display 1 should always work. Choosing any other display should also\n"
            "work, as long as its custom display scaling setting is the same as\n"
            "that of Display 1. If its scaling setting is DIFFERENT from Display 1's\n"
            "scaling setting, the fullscreen window's size, position and UI size\n"
            "might get messed up, making it unusable as an overlay.\n\n"
            "If you encounter these issues, try selecting another display.\n"
            "If you need to use a specific display that has these issues, try "
            "setting its\n"
            "custom display scaling setting to the same value that display 1 has.\n"
            "In Microsoft Windows, you can do that at:\n\n"
            "  -> Settings > System > Display > Scale and layout\n\n"
            "There, the relevant setting is the percentage value, that increases the\n"
            "size of text and apps. Set that percentage to the same as Display 1's.\n"
            "Then restart DZSimulator to see if that fixed it."
        );

        if (win_mode != GuiState::VideoSettings::FULLSCREEN_WINDOWED)
            ImGui::EndDisabled();
    }

    ImGui::Text("");

#ifndef DZSIM_WEB_PORT
    { // Overlay setting
        ImGui::Checkbox("Enable overlay mode",
            &_gui_state.video.IN_overlay_mode_enabled);
        ImGui::SameLine(); _gui.HelpMarker(
            ">>>> Allows to use this program as an overlay for CS:GO.\n"
            "This window will turn transparent and stay always on top of other "
            "windows.\n"
            "Additionally, it becomes click-through if it's not focused.\n"
            "Hence, you need to Alt+Tab to this window to focus it and click in it again.\n"
            "The overlay only works with CS:GO if you also set the following in CS:GO:\n"
            "\n"
            "  -> Settings menu > Video > Display Mode > Fullscreen Windowed\n"
            "\n"
            "The overlay becomes really useful together with these DZSimulator settings:\n"
            "\n"
            "  -> Visualizations > Geometry Visualization Mode > Glidability for player "
            "in local CS:GO session\n"
            "  -> Video Settings > Display Mode > Fullscreen Windowed\n"
            "  -> Video Settings > Display Selection > SAME_DISPLAY_AS_CSGO\n"
            "\n"
            "Then, jump into a local CS:GO match, load the same map in DZSimulator,\n"
            "make sure DZSimulator is connected to the local CS:GO session and enjoy!"
        );

        if (!_gui_state.video.IN_overlay_mode_enabled)
            ImGui::BeginDisabled();

        const float MIN_OVERLAY_TRANSP =  0.0f; // percent
        const float MAX_OVERLAY_TRANSP = 90.0f; // percent
        ImGui::SliderFloat("Overlay Transparency",
            &_gui_state.video.IN_overlay_transparency,
            MIN_OVERLAY_TRANSP, MAX_OVERLAY_TRANSP,
            "%.1f%%", ImGuiSliderFlags_AlwaysClamp);
        bool slider_being_dragged = ImGui::IsItemActive();

        _gui_state.video.IN_overlay_transparency_is_being_adjusted
            = slider_being_dragged;

        if (!_gui_state.video.IN_overlay_mode_enabled)
            ImGui::EndDisabled();
    }

    ImGui::Text("");
#endif

    // GUI scale setting
    int max_gui_scale_slider_pct = 100.0f * _gui.MAX_USER_GUI_SCALING_FACTOR;
    if (max_gui_scale_slider_pct <= _gui._min_user_gui_scaling_factor_pct)
        max_gui_scale_slider_pct = _gui._min_user_gui_scaling_factor_pct * 2;

    if (ImGui::DragInt("GUI Scaling", &_gui_state.video.IN_user_gui_scaling_factor_pct,
        1, _gui._min_user_gui_scaling_factor_pct, max_gui_scale_slider_pct,
        "%d%%", ImGuiSliderFlags_NoInput)) {
        _gui._gui_scaling_update_required = true;
    }
    ImGui::SameLine(); _gui.HelpMarker(">>>> Click and drag to edit value");
}

std::string MenuWindow::GetDisplayName(int idx, int w, int h)
{
    return "Display " + std::to_string(idx)
        + " (" + std::to_string(w) + "x" + std::to_string(h) + ")";
}

void MenuWindow::DrawMovementDebugging()
{
    if (!sim::ENABLE_MOVEMENT_DEBUGGING)
        return;

    using namespace sim;
    const CsgoMovement& mv = _gui_state.mv_debug.OUT_csgo_mv;

    ImGui::PushFont(_gui._font_mono); // Select monospace font

    switch (mv.m_MoveType) {
        case MOVETYPE_NONE:   ImGui::Text("m_MoveType = MOVETYPE_NONE"); break;
        case MOVETYPE_WALK:   ImGui::Text("m_MoveType = MOVETYPE_WALK"); break;
        case MOVETYPE_NOCLIP: ImGui::Text("m_MoveType = MOVETYPE_NOCLIP"); break;
        case MOVETYPE_LADDER: ImGui::Text("m_MoveType = MOVETYPE_LADDER"); break;
        default:              ImGui::Text("m_MoveType = %i", mv.m_MoveType); break;
    }
    ImGui::Text(mv.m_hGroundEntity ? "m_hGroundEntity = true" : "m_hGroundEntity = false");
    ImGui::Text("m_fFlags = %i", mv.m_fFlags);
    ImGui::Text(mv.m_bDucked     ? "m_bDucked = true"     : "m_bDucked = false");
    ImGui::Text(mv.m_bDucking    ? "m_bDucking = true"    : "m_bDucking = false");
    ImGui::Text("m_flDucktime     = %.1F", mv.m_flDucktime);
    ImGui::Text("m_flFallVelocity = %.1F", mv.m_flFallVelocity);
    ImGui::Text(mv.m_bAllowAutoMovement ? "m_bAllowAutoMovement = true" : "m_bAllowAutoMovement = false");
    ImGui::Text("m_flMaxSpeed = %.2F", mv.m_flMaxSpeed);
    ImGui::Text("m_flForwardMove = %.2F", mv.m_flForwardMove);
    ImGui::Text("m_flSideMove    = %.2F", mv.m_flSideMove);
    ImGui::Text("m_nButtons    = %i", mv.m_nButtons);
    ImGui::Text("m_nOldButtons = %i", mv.m_nOldButtons);
    ImGui::Text("m_vecViewOffset = (%.3F, %.3F, %.3F)",
                mv.m_vecViewOffset.x(),
                mv.m_vecViewOffset.y(),
                mv.m_vecViewOffset.z());
    ImGui::Text("m_vecViewAngles = (%.1F, %.1F, %.1F)",
                mv.m_vecViewAngles.x(),
                mv.m_vecViewAngles.y(),
                mv.m_vecViewAngles.z());
    ImGui::Text("m_vecAbsOrigin = (%.3F, %.3F, %.3F)",
                mv.m_vecAbsOrigin.x(),
                mv.m_vecAbsOrigin.y(),
                mv.m_vecAbsOrigin.z());
    ImGui::Text("hori speed    = %.3F", mv.m_vecVelocity.xy().length());
    ImGui::Text("m_vecVelocity = (%.2F, %.2F, %.2F)",
                mv.m_vecVelocity.x(),
                mv.m_vecVelocity.y(),
                mv.m_vecVelocity.z());
    ImGui::Text("m_vecBaseVelocity = (%.2F, %.2F, %.2F)",
                mv.m_vecBaseVelocity.x(),
                mv.m_vecBaseVelocity.y(),
                mv.m_vecBaseVelocity.z());
    ImGui::Text("m_outJumpVel = (%.2F, %.2F, %.2F)",
                mv.m_outJumpVel.x(),
                mv.m_outJumpVel.y(),
                mv.m_outJumpVel.z());
    ImGui::Text("m_iSpeedCropped = %i", mv.m_iSpeedCropped);
    ImGui::Text("m_surfaceFriction = %.4F", mv.m_surfaceFriction);

    ImGui::PopFont();
}

void MenuWindow::DrawCollisionDebugging()
{
#ifndef NDEBUG
    coll::Debugger::DrawImGuiElements(_gui_state);
#endif
}

void MenuWindow::DrawTestSettings()
{
#ifndef NDEBUG
    if (ImGui::Button("Show ImGui Demo Window"))
        _gui_state.show_window_demo ^= true;

    ImGui::SliderFloat("Slider 1",
        &_gui_state.testing.IN_slider1, 0.0f, 0.5f, "%.3f");

    ImGui::SliderFloat("Slider 2",
        &_gui_state.testing.IN_slider2, 0.0f, 5.0f, "%.3f");

    ImGui::SliderFloat("Slider 3",
        &_gui_state.testing.IN_slider3, 0.0f, 10.0f, "%.3f");

    ImGui::SliderFloat("Slider 4",
        &_gui_state.testing.IN_slider4, 0.0f, 10.0f, "%.3f");

#endif
}
