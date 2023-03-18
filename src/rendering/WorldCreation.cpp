#include "WorldCreation.h"

#include <algorithm>
#include <set>

// Include these 2 headers so that GL::Buffer::setData() accepts std::vector
#include <Corrade/Containers/Array.h> 
#include <Corrade/Containers/ArrayViewStl.h>

#include <Magnum/Math/Matrix4.h>
#include <Magnum/MeshTools/Compile.h>
#include <Magnum/Primitives/UVSphere.h>
#include <Magnum/Shaders/GenericGL.h>
#include <Magnum/Trade/MeshData.h>

#include "CsgoConstants.h"
#include "csgo_parsing/AssetFileReader.h"
#include "csgo_parsing/AssetFinder.h"
#include "csgo_parsing/PhyModelParsing.h"
#include "csgo_parsing/utils.h"
#include "GlidabilityShader3D.h"
#include "utils_3d.h"

using namespace rendering;
using namespace rendering::WorldCreation;
using namespace utils_3d;

using namespace Magnum;
using namespace Math::Literals;
namespace BrushSep = csgo_parsing::BrushSeparation;

std::unique_ptr<CsgoMapGeometry> rendering::WorldCreation::CreateCsgoMapGeometry(
    std::shared_ptr<const csgo_parsing::BspMap> bsp_map,
    std::string* dest_errors)
{
    auto geo = std::make_unique<CsgoMapGeometry>();
    std::string error_msgs = "";

    {
        Debug{} << "Parsing displacement face mesh";
        auto displacementFaces = bsp_map->GetDisplacementFaceVertices();
        geo->mesh_displacements =
            GenMeshWithVertAttr_Position_Normal(displacementFaces);
        //MeshGenerator::GenStaticColoredMeshFromFaces(displacementFaces);
    } // Destruct face array once it's no longer needed (reduce peak RAM usage)

    // Idea: Instead of destructing face array, just .clear() it and reuse it
    // for displacement boundary faces?

    {
        Debug{} << "Parsing displacement boundary mesh";
        auto displacementBoundaryFaces = bsp_map->GetDisplacementBoundaryFaceVertices();
        geo->mesh_displacement_boundaries =
            GenMeshWithVertAttr_Position(displacementBoundaryFaces);
    } // Destruct face array once it's no longer needed (reduce peak RAM usage)

    
    // ---- Collect all ".mdl" and ".phy" files from the packed files
    std::vector<uint16_t> packed_mdl_file_indices; // indices into BspMap::packed_files
    std::vector<uint16_t> packed_phy_file_indices; // indices into BspMap::packed_files
    for (size_t i = 0; i < bsp_map->packed_files.size(); i++) {
        const std::string& fname = bsp_map->packed_files[i].file_name;
        if (fname.length() >= 5) {
            if      (fname.ends_with(".mdl")) packed_mdl_file_indices.push_back(i);
            else if (fname.ends_with(".phy")) packed_phy_file_indices.push_back(i);
        }
    }
    // ---- Sort packed file indices by file name to enable fast lookup later
    auto comp__packed_file_name = [&](uint16_t idx_a, uint16_t idx_b) {
        return bsp_map->packed_files[idx_a].file_name < bsp_map->packed_files[idx_b].file_name;
    };
    std::sort(
        packed_mdl_file_indices.begin(),
        packed_mdl_file_indices.end(),
        comp__packed_file_name);
    std::sort(
        packed_phy_file_indices.begin(),
        packed_phy_file_indices.end(),
        comp__packed_file_name);

    for (auto packed_file_idx : packed_mdl_file_indices)
        Debug{} << "packed MDL:" << bsp_map->packed_files[packed_file_idx].file_name.c_str();
    for (auto packed_file_idx : packed_phy_file_indices)
        Debug{} << "packed PHY:" << bsp_map->packed_files[packed_file_idx].file_name.c_str();

    // predicate function used for binary lookup of packed file idx with file name
    auto comp__find_packed_file_name_idx =
        [&](uint16_t packed_file_idx, const std::string& file_name) {
        return bsp_map->packed_files[packed_file_idx].file_name < file_name;
    };

    // ---- Load collision models of solid prop_static entities 

    // key:   ".mdl" file path referenced by at least one solid static prop
    // value: Corresponding collision model mesh
    std::map<std::string, Magnum::GL::Mesh> sprop_coll_meshes;

    // MDL paths that we already attempted to load the collision model of
    std::set<std::string> checked_mdl_paths;

    for (const csgo_parsing::BspMap::StaticProp& sprop : bsp_map->static_props) {
        // Models can be referenced by solid and non-solid static props at the same time!
        if (!sprop.IsSolidWithVPhysics())
            continue;

        // Path to ".mdl" file used by static prop
        const std::string& mdl_path = bsp_map->static_prop_model_dict[sprop.model_idx];

        if (checked_mdl_paths.contains(mdl_path))
            continue; // Skip, we already tried to load this MDL's collision 
        checked_mdl_paths.insert(mdl_path); // Remember this MDL's load attempt

        if (mdl_path.length() < 5) // Ensure valid file path
            continue;
        std::string phy_path = mdl_path;
        phy_path[phy_path.length() - 3] = 'p';
        phy_path[phy_path.length() - 2] = 'h';
        phy_path[phy_path.length() - 1] = 'y';

        // Search for MDL file in packed files
        auto it_packed_mdl_idx = std::lower_bound(
            packed_mdl_file_indices.begin(),
            packed_mdl_file_indices.end(),
            mdl_path,
            comp__find_packed_file_name_idx);
        bool is_mdl_in_packed_files =
            it_packed_mdl_idx != packed_mdl_file_indices.end() &&
            mdl_path.compare(bsp_map->packed_files[*it_packed_mdl_idx].file_name) == 0;

        // Search for PHY file in packed files
        auto it_packed_phy_idx = std::lower_bound(
            packed_phy_file_indices.begin(),
            packed_phy_file_indices.end(),
            phy_path,
            comp__find_packed_file_name_idx);
        bool is_phy_in_packed_files =
            it_packed_phy_idx != packed_phy_file_indices.end() &&
            phy_path.compare(bsp_map->packed_files[*it_packed_phy_idx].file_name) == 0;

        bool is_mdl_in_game_files =
            csgo_parsing::AssetFinder::ExistsInGameFiles(mdl_path);

        // We require every solid prop to have an existing ".mdl" file
        if (!is_mdl_in_game_files && !is_mdl_in_packed_files) {
            error_msgs += "Failed to find MDL file '" + mdl_path + "', "
                "referenced by at least one solid prop_static, e.g. at origin=("
                + std::to_string((int64_t)sprop.origin.x()) + ","
                + std::to_string((int64_t)sprop.origin.y()) + ","
                + std::to_string((int64_t)sprop.origin.z()) + "). "
                "All prop_static of this type will be missing from the world.\n";
            continue;
        }

        GL::Mesh phy_mesh{ NoCreate };
        csgo_parsing::utils::RetCode phy_parse_status;
        
        if (is_phy_in_packed_files) {
            phy_parse_status = CreatePhyModelMeshFromPackedPhyFile(&phy_mesh,
                bsp_map->abs_bsp_file_path,
                bsp_map->packed_files[*it_packed_phy_idx].file_offset,
                bsp_map->packed_files[*it_packed_phy_idx].file_len);
        }
        else {
            bool is_phy_in_game_files =
                csgo_parsing::AssetFinder::ExistsInGameFiles(phy_path);
            if (is_phy_in_game_files) {
                phy_parse_status =
                    CreatePhyModelMeshFromGameFile(&phy_mesh, phy_path);
            }
            else {
                // Static prop is non-solid if their model's PHY doesn't exist anywhere
                continue; // Not an error, we just skip this non-solid model
            }
        }

        if (phy_parse_status.successful()) {
            sprop_coll_meshes[mdl_path] = std::move(phy_mesh);
        }
        else { // If PHY mesh creation failed
            error_msgs += "All prop_static using the model '" + mdl_path + "' "
                "will be missing from the world because loading their "
                "collision model failed:\n    " + phy_parse_status.desc_msg + "\n";
        }
    }

    struct InstanceData {
        Matrix4 model_transformation; // model scale, rotation, translation
        //Color3 color; // other attributes are possible
    };
    // key is MDL name, value is list of its static prop's transformation matrices
    std::map<std::string, std::vector<InstanceData>> sprop_instance_data;
    for (const csgo_parsing::BspMap::StaticProp& sprop : bsp_map->static_props) {
        if (!sprop.IsSolidWithVPhysics())
            continue;

        // We only care about static props with successfully loaded collision models
        const auto& mdl_path = bsp_map->static_prop_model_dict[sprop.model_idx];
        if (!sprop_coll_meshes.contains(mdl_path))
            continue;

        // Compute static prop's transformation matrix
        InstanceData inst_d = {
            utils_3d::CalcModelTransformationMatrix(
                sprop.origin, sprop.angles, sprop.uniform_scale)
        };
        sprop_instance_data[mdl_path].push_back(inst_d);
    }

    for (auto& kv : sprop_instance_data) {
        const std::string& mdl_path = kv.first;
        std::vector<InstanceData>& instances = kv.second;
        GL::Mesh& mesh = sprop_coll_meshes[mdl_path];

        mesh.setInstanceCount(instances.size())
            .addVertexBufferInstanced(
                GL::Buffer{ std::move(instances) },
                1,
                0,
                GlidabilityShader3D::TransformationMatrix{}
                //, GlidabilityShader3D::Color3{} // other attributes are possible
        );

        geo->instanced_static_prop_meshes.emplace_back(std::move(mesh));
    }


    // ----- BRUSHES
    Debug{} << "Parsing model brush indices";
    std::vector<std::set<size_t>> bmodel_brush_indices;
    for (size_t i = 0; i < bsp_map->models.size(); ++i)
        bmodel_brush_indices.push_back(std::move(bsp_map->GetModelBrushIndices(i)));
    // bmodel at idx 0 is worldspawn, containing most map geometry
    // all other bmodels are tied to brush entities
    std::set<size_t>& worldspawn_brush_indices = bmodel_brush_indices[0];

    Debug{} << "Calculating func_brush rotation transformations";
    // Calculate rotation transformation for every SOLID func_brush entity, whose angles are not { 0, 0, 0 }
    std::map<const csgo_parsing::BspMap::Ent_func_brush*, Matrix4> func_brush_rot_transformations;
    for (size_t i = 0; i < bsp_map->entities_func_brush.size(); ++i) {
        auto& func_brush = bsp_map->entities_func_brush[i];
        if (!func_brush.IsSolid()) continue;
        if (func_brush.angles[0] == 0.0f && func_brush.angles[1] == 0.0f && func_brush.angles[2] == 0.0f)
            continue;

        // Order of axis rotations is important! First roll, then pitch, then yaw rotation!
        func_brush_rot_transformations[&func_brush] =
            Matrix4::rotationZ(Deg{ func_brush.angles[1] }) * // (yaw)   rotation around z axis
            Matrix4::rotationY(Deg{ func_brush.angles[0] }) * // (pitch) rotation around y axis
            Matrix4::rotationX(Deg{ func_brush.angles[2] });  // (roll)  rotation around x axis
    }

    // Keep this list in the same order as the enum declaration, so that a brush
    // category can be identified by index
    std::vector<BrushSep::Category> bCategories = {
        BrushSep::Category::OTHER,
        BrushSep::Category::SOLID,
        BrushSep::Category::PLAYERCLIP,
        BrushSep::Category::GRENADECLIP,
        BrushSep::Category::LADDER,
        BrushSep::Category::WATER,
        BrushSep::Category::SKY
    };

    for (size_t i = 0; i < bCategories.size(); ++i) {
        BrushSep::Category brushCat = bCategories[i];
        Debug{} << "Parsing brush category" << brushCat;

        auto testFuncs = BrushSep::getBrushCategoryTestFuncs(brushCat);
        std::vector<std::vector<Vector3>> faces =
            bsp_map->GetBrushFaceVertices(worldspawn_brush_indices, testFuncs.first,
                testFuncs.second);

        // Look for additional brushes from the current category in func_brush entities
        for (auto& func_brush : bsp_map->entities_func_brush) {
            if (!func_brush.IsSolid())
                continue;
            // Special case: grenadeclip brushes don't work in func_brush entities (for unknown reasons)
            if (brushCat == BrushSep::Category::GRENADECLIP)
                continue;

            if (func_brush.model.size() == 0 || func_brush.model[0] != '*') continue;
            std::string idxStr = func_brush.model.substr(1);
            int64_t modelIdx = csgo_parsing::utils::ParseIntFromString(idxStr, -1);
            if (modelIdx <= 0 || modelIdx >= (int64_t)bsp_map->models.size()) {
                error_msgs += "Failed to load func_brush at origin=("
                    + std::to_string((int64_t)func_brush.origin.x()) + ","
                    + std::to_string((int64_t)func_brush.origin.y()) + ","
                    + std::to_string((int64_t)func_brush.origin.z()) + "), "
                    "it has an invalid model idx.\n";
                continue;
            }

            auto& brush_indices = bmodel_brush_indices[modelIdx];
            auto faces_from_func_brush = bsp_map->GetBrushFaceVertices(brush_indices,
                testFuncs.first, testFuncs.second);
            if (faces_from_func_brush.size() == 0) continue;

            // Rotate and translate every vertex with func_brush's origin and angle
            bool is_func_brush_rotated = func_brush.angles[0] != 0.0f || func_brush.angles[1] != 0.0f || func_brush.angles[2] != 0.0f;
            Matrix4* rotTransformation = is_func_brush_rotated ? &func_brush_rot_transformations[&func_brush] : nullptr;
            for (auto& face : faces_from_func_brush) {
                for (auto& v : face) {
                    if (is_func_brush_rotated) // Rotate vertex if func_brush has a non-zero angle
                        v = (*rotTransformation).transformVector(v); // rotate point around origin
                    v += func_brush.origin; // translate point
                }
            }
            // Append new faces
            faces.insert(faces.end(),
                std::make_move_iterator(faces_from_func_brush.begin()),
                std::make_move_iterator(faces_from_func_brush.end()));
        }

        // Remove all water faces that are not facing upwards. We draw water
        // with transparency, so we dont want water faces other than those
        // representing the water surface
        if (brushCat == BrushSep::Category::WATER) {
            std::vector<std::vector<Vector3>> water_surface_faces;
            for (auto& face : faces) {
                // faces have clockwise vertex winding
                if (utils_3d::IsCwTriangleFacingUp(face[0], face[1], face[2]))
                    water_surface_faces.push_back(std::move(face));
            }
            faces = std::move(water_surface_faces);
        }

        geo->brush_category_meshes[brushCat] =
            GenMeshWithVertAttr_Position_Normal(faces);
    }

    // ----- trigger_push BRUSHES (only use those that push players)
    std::vector<std::vector<Vector3>> trigger_push_faces;
    for (const auto& trigger_push : bsp_map->entities_trigger_push) {
        if (!trigger_push.CanPushPlayers())
            continue;
        if (trigger_push.model.size() == 0 || trigger_push.model[0] != '*')
            continue;
        std::string idx_str = trigger_push.model.substr(1);
        int64_t model_idx = csgo_parsing::utils::ParseIntFromString(idx_str, -1);
        if (model_idx <= 0 || model_idx >= (int64_t)bsp_map->models.size()) {
            error_msgs += "Failed to load trigger_push at origin=("
                + std::to_string((int64_t)trigger_push.origin.x()) + ","
                + std::to_string((int64_t)trigger_push.origin.y()) + ","
                + std::to_string((int64_t)trigger_push.origin.z()) + "), "
                "it has an invalid model idx.\n";
            continue;
        }
        auto& brush_indices = bmodel_brush_indices[model_idx];
        auto faces_from_trigger_push = bsp_map->GetBrushFaceVertices(brush_indices);
        if (faces_from_trigger_push.size() == 0) continue;

        // Rotate and translate model of trigger_push.
        // Elevate above water surface to fix Z fighting with the water
        const Vector3 Z_FIGHTING_RESOLVER = {0.0f, 0.0f, 0.1f};
        Matrix4 trigger_push_transf = utils_3d::CalcModelTransformationMatrix(
            trigger_push.origin + Z_FIGHTING_RESOLVER,
            trigger_push.angles
        );
        for (auto& face : faces_from_trigger_push) {
            for (auto& v : face) {
                v = trigger_push_transf.transformPoint(v);
            }
        }

        trigger_push_faces.insert(trigger_push_faces.end(),
            std::make_move_iterator(faces_from_trigger_push.begin()),
            std::make_move_iterator(faces_from_trigger_push.end()));
    }
    geo->trigger_push_meshes =
        GenMeshWithVertAttr_Position_Normal(trigger_push_faces);


    // Construct player trajectory
    const float TRAJ_TICK_RATE = 64.0f;
    const float TRAJ_TICK_LENGTH = 1.0f / TRAJ_TICK_RATE; // seconds
    const size_t TRAJ_TICK_COUNT = 6 * TRAJ_TICK_RATE;
    const float TRAJ_WIDTH = 20.0f;
    float vel_z = 0.0f;
    float pos_z = 0.0f;
    std::vector<std::vector<Vector3>> traj_triangles;
    traj_triangles.reserve(2 * TRAJ_TICK_COUNT);

    for (size_t tick = 0; tick < TRAJ_TICK_COUNT; tick++) {
        float new_vel_z = vel_z + TRAJ_TICK_LENGTH * -CSGO_CVAR_SV_GRAVITY;
        float new_pos_z = pos_z + TRAJ_TICK_LENGTH * vel_z;

        Vector3 top1 = Vector3(TRAJ_TICK_LENGTH * tick, -0.5f * TRAJ_WIDTH, pos_z );
        Vector3 top2 = Vector3(TRAJ_TICK_LENGTH * tick, +0.5f * TRAJ_WIDTH, pos_z );
        Vector3 bot1 = Vector3(TRAJ_TICK_LENGTH * (tick + 1), -0.5f * TRAJ_WIDTH, new_pos_z );
        Vector3 bot2 = Vector3(TRAJ_TICK_LENGTH * (tick + 1), +0.5f * TRAJ_WIDTH, new_pos_z );

        traj_triangles.emplace_back(std::vector<Vector3>{ top1, top2, bot1 });
        traj_triangles.emplace_back(std::vector<Vector3>{ top2, bot2, bot1 });

        vel_z = new_vel_z;
        pos_z = new_pos_z;
    }

    geo->unit_trajectory_mesh = GenMeshWithVertAttr_Position(traj_triangles);


    if (dest_errors)
        *dest_errors = std::move(error_msgs);
    return geo;
}

csgo_parsing::utils::RetCode
rendering::WorldCreation::CreatePhyModelMeshFromGameFile(GL::Mesh* dest,
    const std::string& src_phy_path)
{
    if (src_phy_path.length() < 5)
        return {
            csgo_parsing::utils::RetCode::ERROR_PHY_PARSING_FAILED,
            "Invalid PHY file path"
        };

    // Start reading PHY file from game dir and VPK archives
    csgo_parsing::AssetFileReader reader;
    if (!reader.OpenFileFromGameFiles(src_phy_path))
        return {
            csgo_parsing::utils::RetCode::ERROR_PHY_PARSING_FAILED,
            "Failed to open PHY file from game files"
        };

    // CSGO loads the phy model even if checksum of MDL and PHY are not identical.
    // Parse with reader that is opened in PHY file
    std::vector<std::vector<Vector3>> triangles;
    std::string surface_property;
    auto ret = csgo_parsing::ParsePhyModel(&triangles, &surface_property, reader);

    if (!ret) // If parsing failed, pass error code along
        return ret;
    if (dest)
        *dest = GenMeshWithVertAttr_Position_Normal(triangles);
    return { csgo_parsing::utils::RetCode::SUCCESS };
}

csgo_parsing::utils::RetCode
rendering::WorldCreation::CreatePhyModelMeshFromPackedPhyFile(
    GL::Mesh* dest,
    const std::string& abs_bsp_file_path,
    size_t packed_phy_file_pos,
    size_t packed_phy_file_len)
{
    // Start reading PHY file from within a BSP map file
    csgo_parsing::AssetFileReader reader;
    if (!reader.OpenFileFromAbsolutePath(abs_bsp_file_path))
        return { csgo_parsing::utils::RetCode::ERROR_PHY_PARSING_FAILED,
            "Failed to open BSP file for parsing a packed PHY file: "
                + abs_bsp_file_path
        };

    if (!reader.SetPos(packed_phy_file_pos))
        return { csgo_parsing::utils::RetCode::ERROR_PHY_PARSING_FAILED,
            "Failed to parse packed PHY file, BSP file seek failed, pos "
                + std::to_string(packed_phy_file_pos)
        };

    // CSGO loads the phy model even if checksum of MDL and PHY are not identical.
    // Parse with reader that is opened in PHY file
    std::vector<std::vector<Vector3>> triangles;
    std::string surface_property;
    auto ret = csgo_parsing::ParsePhyModel(&triangles, &surface_property,
        reader, packed_phy_file_len);

    if (!ret) // If parsing failed, pass error code along
        return ret;
    if (dest)
        *dest = GenMeshWithVertAttr_Position_Normal(triangles);
    return { csgo_parsing::utils::RetCode::SUCCESS };
}

GL::Mesh rendering::WorldCreation::CreateBumpMineMesh()
{
    return MeshTools::compile(Primitives::uvSphereSolid(7, 10));
}

GL::Mesh rendering::WorldCreation::GenMeshWithVertAttr_Position(
    const std::vector<std::vector<Vector3>>& faces)
{
    struct Vert {
        Vector3 position;
    };

    std::vector<Vert> data_vertbuf;

    // Turn faces into triangles
    for (const std::vector<Vector3>& face : faces) {
        for (size_t tri = 0; tri < face.size() - 2; ++tri) {
            data_vertbuf.emplace_back(face[0]);
            data_vertbuf.emplace_back(face[tri + 1]);
            data_vertbuf.emplace_back(face[tri + 2]);
        }
    }

    GL::Buffer vertices;
    vertices.setData(data_vertbuf);
    GL::Mesh mesh;
    mesh.setCount(vertices.size() / sizeof(Vert))
        .addVertexBuffer(std::move(vertices), 0,
            Shaders::GenericGL3D::Position{});

    return mesh;
}

GL::Mesh rendering::WorldCreation::GenMeshWithVertAttr_Position_Normal(
    const std::vector<std::vector<Vector3>>& faces)
{
    struct Vert {
        Vector3 position;
        Vector3 normal;
    };

    std::vector<Vert> data_vertbuf;

    // Turn faces into triangles
    for (const std::vector<Vector3>& face : faces) {
        for (size_t tri = 0; tri < face.size() - 2; ++tri) {
            // individual normal calculation seems to be required, although triangles all face in the same direction
            Vector3 normal = CalcNormalCwFront(face[0], face[tri + 1], face[tri + 2]);

            Vert vert1 = { face[0], normal };
            Vert vert2 = { face[tri + 1], normal };
            Vert vert3 = { face[tri + 2], normal };
            data_vertbuf.push_back(vert1);
            data_vertbuf.push_back(vert2);
            data_vertbuf.push_back(vert3);
        }
    }

    GL::Buffer vertices;
    vertices.setData(data_vertbuf);
    GL::Mesh mesh;
    mesh.setCount(vertices.size() / sizeof(Vert))
        .addVertexBuffer(std::move(vertices), 0,
            Shaders::GenericGL3D::Position{},
            Shaders::GenericGL3D::Normal{});

    return mesh;
}
