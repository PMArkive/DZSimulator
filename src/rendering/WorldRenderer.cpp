#include "WorldRenderer.h"

#include <cmath>
#include <string>
#include <vector>

#include <Magnum/GL/Renderer.h>
#include <Magnum/Magnum.h>
#include <Magnum/Math/Angle.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Math/Functions.h>
#include <Magnum/Math/Matrix4.h>

#include "CsgoConstants.h"
#include "csgo_parsing/BrushSeparation.h"
#include "CSGOConstants.h"
#include "utils_3d.h"

using namespace Magnum;
using namespace Math::Literals;
using namespace rendering;

namespace BrushSep = csgo_parsing::BrushSeparation;

WorldRenderer::WorldRenderer(const Utility::Resource& resources,
        gui::GuiState& gui_state)
    : _resources { resources }
    , _gui_state { gui_state }
{
}

void WorldRenderer::InitShaders()
{
    _glid_shader_instanced     = GlidabilityShader3D{  true, _resources };
    _glid_shader_non_instanced = GlidabilityShader3D{ false, _resources };
    _flat_shader = Shaders::FlatGL3D{ };
}

void WorldRenderer::UnloadGeometry()
{
    _map_geo.reset(); // Destruct all mesh data
}

void WorldRenderer::LoadBspMapGeometry(
    std::shared_ptr<const csgo_parsing::BspMap> bsp_map)
{
    UnloadGeometry(); // Make sure previous map geometry is deallocated
    
    // TODO only create this once, not every map load
    _mesh_bump_mine = WorldCreation::CreateBumpMineMesh();

    std::string map_geo_creation_errors;
    _map_geo =
        WorldCreation::CreateCsgoMapGeometry(bsp_map, &map_geo_creation_errors);

    if (!map_geo_creation_errors.empty()) {
        Debug{} << map_geo_creation_errors.c_str();
        _gui_state.popup.QueueMsgWarn(map_geo_creation_errors);
    }
}

void WorldRenderer::Draw(const Matrix4& view_proj_transformation,
    const Vector3& player_feet_pos,
    const Vector3& player_velocity,
    const std::vector<Vector3>& bump_mine_positions)
{
    Deg hori_light_angle{ _gui_state.vis.IN_hori_light_angle };
    Vector3 light_dir(
        Math::cos(hori_light_angle),
        Math::sin(hori_light_angle),
        0.0f
    ); // vector must be normalized

    // Don't do lighting in overlay, it is inaccurate compared to CSGO's lighting
    bool has_world_diffuse_lighting = !_gui_state.video.IN_overlay_mode_enabled;

    bool glidability_vis_globally_disabled =
        _gui_state.vis.IN_geo_vis_mode != _gui_state.vis.GLID_AT_SPECIFIC_SPEED &&
        _gui_state.vis.IN_geo_vis_mode != _gui_state.vis.GLID_OF_CSGO_SESSION;

    float glid_shader_hori_speed = player_velocity.xy().length();
    if (glid_shader_hori_speed < 1.0)
        glid_shader_hori_speed = 1.0;

    // Set some uniforms for both glidability shaders
    for (size_t i = 0; i < 2; i++) {
        // TODO Put uniforms that are shared between multiple shaders in a
        //      uniform buffer?
        GlidabilityShader3D& glid_shader = (i == 0) ?
            _glid_shader_instanced :
            _glid_shader_non_instanced;

        glid_shader
            .SetLightDirection(light_dir)
            .SetPlayerPosition(player_feet_pos)
            .SetHorizontalPlayerSpeed(glid_shader_hori_speed);

        // Game settings
        glid_shader
            .SetGravity(CSGO_CVAR_SV_GRAVITY)
            .SetMinNoGroundChecksVelZ(CSGO_CONST_MIN_NO_GROUND_CHECKS_VEL_Z)
            .SetMaxVelocity(CSGO_CVAR_SV_MAXVELOCITY)
            .SetStandableNormal(CSGO_CVAR_SV_STANDABLE_NORMAL);
    }

    GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);
    GL::Renderer::setFrontFace(GL::Renderer::FrontFace::ClockWise);
    GL::Renderer::setPolygonMode(GL::Renderer::PolygonMode::Fill);
    //GL::Renderer::setPolygonMode(GL::Renderer::PolygonMode::Line);
    //GL::Renderer::setLineWidth(1.0f);

    // Draw displacements
    _glid_shader_non_instanced
        .SetFinalTransformationMatrix(view_proj_transformation)
        // gray-yellow-orange
        .SetOverrideColor(CvtImguiCol4(_gui_state.vis.IN_col_solid_displacements))
        .SetColorOverrideEnabled(glidability_vis_globally_disabled)
        .SetDiffuseLightingEnabled(has_world_diffuse_lighting)
        .draw(_map_geo->mesh_displacements);

    // Draw displacement boundaries
    if (_gui_state.vis.IN_draw_displacement_edges)
        _flat_shader
            .setTransformationProjectionMatrix(view_proj_transformation)
            .setColor(CvtImguiCol4(_gui_state.vis.IN_col_solid_disp_boundary))
            .draw(_map_geo->mesh_displacement_boundaries);

    // Draw bump mines - they're currently the only thing drawn with CCW vertex winding
    // TODO use instancing?
    GL::Renderer::setFrontFace(GL::Renderer::FrontFace::CounterClockWise);
    for (Vector3 bm_pos : bump_mine_positions) {
        Matrix4 model_transformation =
            utils_3d::CalcModelTransformationMatrix(bm_pos,
                { 0.0f, 0.0f, 0.0f }, 20.0f);
        Matrix4 mvp_transformation =
            view_proj_transformation * model_transformation;

        _glid_shader_non_instanced
            .SetFinalTransformationMatrix(mvp_transformation)
            .SetOverrideColor(CvtImguiCol4(_gui_state.vis.IN_col_bump_mine))
            .SetColorOverrideEnabled(true)
            .SetDiffuseLightingEnabled(has_world_diffuse_lighting)
            .draw(_mesh_bump_mine);
    }
    GL::Renderer::setFrontFace(GL::Renderer::FrontFace::ClockWise);

    // Draw collision models of static props
    _glid_shader_instanced
        .SetFinalTransformationMatrix(view_proj_transformation)
        .SetColorOverrideEnabled(glidability_vis_globally_disabled)
        .SetOverrideColor(CvtImguiCol4(_gui_state.vis.IN_col_solid_sprops))
        .SetDiffuseLightingEnabled(has_world_diffuse_lighting);
    for (GL::Mesh& instanced_sprop_mesh : _map_geo->instanced_static_prop_meshes) {
        _glid_shader_instanced.draw(instanced_sprop_mesh);
    }

    // TRANSPARENT BRUSHES MUST BE THE LAST THINGS BEING DRAWN

    // Determine draw order of brush categories
    std::vector<BrushSep::Category> brush_cat_draw_order;
    std::vector<BrushSep::Category> last_drawn_brush_cats;
    brush_cat_draw_order.reserve(_map_geo->brush_category_meshes.size());
    for (const auto &kv : _map_geo->brush_category_meshes) {
        BrushSep::Category b_cat = kv.first;

        // Transparent things must be the last things being drawn!
        if (b_cat == BrushSep::Category::WATER ||
            b_cat == BrushSep::Category::GRENADECLIP ||
            b_cat == BrushSep::Category::PLAYERCLIP)
            last_drawn_brush_cats.push_back(b_cat);
        else
            brush_cat_draw_order.push_back(b_cat);
    }
    for (BrushSep::Category b_cat : last_drawn_brush_cats)
        brush_cat_draw_order.push_back(b_cat);
        
    // Draw brush categories
    for (BrushSep::Category b_cat : brush_cat_draw_order) {
        // Determine if brush category's surface glidability is visualized
        bool visualize_glidability = false;
        if (b_cat == BrushSep::Category::SOLID ||
            b_cat == BrushSep::Category::PLAYERCLIP)
            visualize_glidability = true;

        if (glidability_vis_globally_disabled)
            visualize_glidability = false;

        // Determine if current brush category's mesh color should be
        // darkened depending on angle to the light. Sky's and water's color
        // should not be influenced by light positions.
        bool has_brush_mesh_diffuse_lighting = has_world_diffuse_lighting;
        if (b_cat == BrushSep::Category::SKY ||
            b_cat == BrushSep::Category::WATER)
            has_brush_mesh_diffuse_lighting = false;

        // Determine override color
        float unknown_col[4] { 1.0f, 1.0f, 1.0f, 1.0f };
        float* b_col = unknown_col;
        switch (b_cat) {
        case BrushSep::SKY:         b_col = _gui_state.vis.IN_col_sky; break;
        case BrushSep::LADDER:      b_col = _gui_state.vis.IN_col_ladders; break;
        case BrushSep::SOLID:       b_col = _gui_state.vis.IN_col_solid_other_brushes; break;
        case BrushSep::WATER:       b_col = _gui_state.vis.IN_col_water; break;
        case BrushSep::PLAYERCLIP:  b_col = _gui_state.vis.IN_col_player_clip; break;
        case BrushSep::GRENADECLIP: b_col = _gui_state.vis.IN_col_grenade_clip; break;
        }

        _glid_shader_non_instanced
            .SetFinalTransformationMatrix(view_proj_transformation)
            .SetOverrideColor(CvtImguiCol4(b_col))
            .SetColorOverrideEnabled(visualize_glidability == false)
            .SetDiffuseLightingEnabled(has_brush_mesh_diffuse_lighting)
            .draw(_map_geo->brush_category_meshes[b_cat]);
    }

    // Draw player trajectories
    {
        GL::Renderer::disable(GL::Renderer::Feature::FaceCulling);
        float traj_yaw = 0.0f;
        float player_vel_hori = player_velocity.xy().length();
        if (player_vel_hori > 0.01f)
            traj_yaw = std::atan2(player_velocity.y(), player_velocity.x());

        // Determine future or past apex of the player trajectory
        const float PREDICT_TICK_RATE = 64.0f;
        const float PREDICT_TICK_LENGTH = 1.0f / PREDICT_TICK_RATE; // seconds
        Vector3 pos = player_feet_pos;
        Vector3 vel = player_velocity.z() > 0.0f ? player_velocity : -player_velocity;
        while (vel.z() > 0.0f) {
            pos += PREDICT_TICK_LENGTH * vel;
            vel.z() += PREDICT_TICK_LENGTH * -CSGO_CVAR_SV_GRAVITY;
        }
        Vector3 trajectory_apex = pos;

        Matrix4 traj_transformation =
            view_proj_transformation *
            Matrix4::translation(trajectory_apex) *
            Matrix4::rotationZ(Rad(traj_yaw)) *
            Matrix4::scaling(Vector3(player_vel_hori, 1.0f, 1.0f));
        _glid_shader_non_instanced
            .SetFinalTransformationMatrix(traj_transformation)
            .SetOverrideColor({0.5f, 0.0f, 1.0f})
            .SetColorOverrideEnabled(true)
            .SetDiffuseLightingEnabled(false)
            .draw(_map_geo->unit_trajectory_mesh);
        GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);
    }

    // ANYTHING BEING DRAWN AFTER HERE WILL NOT BE VISIBLE BEHIND
    // TRANSPARENT BRUSHES

    // Draw trigger_push entities that can push players
    _glid_shader_non_instanced
        .SetFinalTransformationMatrix(view_proj_transformation)
        .SetOverrideColor(CvtImguiCol4(_gui_state.vis.IN_col_trigger_push))
        .SetColorOverrideEnabled(true)
        .SetDiffuseLightingEnabled(true)
        .draw(_map_geo->trigger_push_meshes);

}

Magnum::Color4 rendering::WorldRenderer::CvtImguiCol4(float* im_col4)
{
    return Magnum::Color4(im_col4[0], im_col4[1], im_col4[2], im_col4[3]);
}
