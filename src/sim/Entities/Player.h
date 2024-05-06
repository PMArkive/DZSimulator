#ifndef SIM_ENTITIES_PLAYER_H_
#define SIM_ENTITIES_PLAYER_H_

#include <vector>

#include <Magnum/Magnum.h>
#include <Magnum/Math/BitVector.h>
#include <Magnum/Math/Tags.h>
#include <Magnum/Math/Time.h>
#include <Magnum/Math/Vector3.h>
#include <Corrade/Utility/Debug.h>

#include "sim/CsgoConstants.h"
#include "sim/PlayerInputState.h"
#include "sim/Sim.h"

namespace sim::Entities {

class Player {
public:
    class Loadout {
    public:
        enum Weapon {
            Fists = 0,
            Knife,
            BumpMine,
            Taser,
            XM1014,
            TOTAL_COUNT // Must be the last enum value!
        };

        // Which weapon the player is currently holding.
        Weapon active_weapon;

        // Flags indicating which weapons are carried by the player, _excluding_
        // the active weapon.
        // A weapon's enum value signifies its bit position in this BitVector.
        using WeaponList = Magnum::Math::BitVector<Weapon::TOTAL_COUNT>;
        WeaponList non_active_weapons;

        bool has_exojump;

        // --------

        bool operator==(const Loadout& other) const {
            return this->active_weapon      == other.active_weapon &&
                   this->non_active_weapons == other.non_active_weapons &&
                   this->has_exojump        == other.has_exojump;
        }
        bool operator!=(const Loadout& other) const {
            return !operator==(other);
        }

        Loadout(bool has_exo, Weapon active,
                const std::vector<Weapon>& non_active_list)
            : active_weapon{ active }
            , non_active_weapons{ Magnum::Math::ZeroInit }
            , has_exojump{ has_exo }
        {
            for (Weapon non_active_weapon : non_active_list)
                if (non_active_weapon != active)
                    non_active_weapons.set(non_active_weapon, true);
        }
    };

    Loadout loadout = Loadout(false, Loadout::Weapon::XM1014, {});

    // Set to simulation time point 0 by default
    SimTimePoint next_primary_attack{ Magnum::Math::ZeroInit };

    // ---- Player input command states
    // inputCmdActiveCount: Each time +cmd is issued, increment the count.
    //                      Each time -cmd is issued, decrement the count.
    // Only decrement if the count is greater than zero.

    unsigned int inputCmdActiveCount_forward   = 0; // default: W key
    unsigned int inputCmdActiveCount_back      = 0; // default: S key
    unsigned int inputCmdActiveCount_moveleft  = 0; // default: A key
    unsigned int inputCmdActiveCount_moveright = 0; // default: D key
    unsigned int inputCmdActiveCount_use       = 0; // default: E key
    unsigned int inputCmdActiveCount_jump      = 0; // default: Space key
    unsigned int inputCmdActiveCount_duck      = 0; // default: Ctrl key
    unsigned int inputCmdActiveCount_speed     = 0; // default: Shift key
    unsigned int inputCmdActiveCount_attack    = 0; // default: Mouse 1 button
    unsigned int inputCmdActiveCount_attack2   = 0; // default: Mouse 2 button
    
    // --------------------------------

    Player() = default;

    unsigned int& inputCmdActiveCount(PlayerInputState::Command cmd) {
        switch (cmd) {
        case PlayerInputState::Command::PLUS_FORWARD:
        case PlayerInputState::Command::MINUS_FORWARD:
            return inputCmdActiveCount_forward;
        case PlayerInputState::Command::PLUS_BACK:
        case PlayerInputState::Command::MINUS_BACK:
            return inputCmdActiveCount_back;
        case PlayerInputState::Command::PLUS_MOVELEFT:
        case PlayerInputState::Command::MINUS_MOVELEFT:
            return inputCmdActiveCount_moveleft;
        case PlayerInputState::Command::PLUS_MOVERIGHT:
        case PlayerInputState::Command::MINUS_MOVERIGHT:
            return inputCmdActiveCount_moveright;
        case PlayerInputState::Command::PLUS_USE:
        case PlayerInputState::Command::MINUS_USE:
            return inputCmdActiveCount_use;
        case PlayerInputState::Command::PLUS_JUMP:
        case PlayerInputState::Command::MINUS_JUMP:
            return inputCmdActiveCount_jump;
        case PlayerInputState::Command::PLUS_DUCK:
        case PlayerInputState::Command::MINUS_DUCK:
            return inputCmdActiveCount_duck;
        case PlayerInputState::Command::PLUS_SPEED:
        case PlayerInputState::Command::MINUS_SPEED:
            return inputCmdActiveCount_speed;
        case PlayerInputState::Command::PLUS_ATTACK:
        case PlayerInputState::Command::MINUS_ATTACK:
            return inputCmdActiveCount_attack;
        case PlayerInputState::Command::PLUS_ATTACK2:
        case PlayerInputState::Command::MINUS_ATTACK2:
            return inputCmdActiveCount_attack2;
        }
        Magnum::Error{} << "[ERROR] Player::inputCmdActiveCount() Unknown cmd! "
                           "Forgot a switch case?";
        std::terminate();
        return inputCmdActiveCount_forward; // irrelevant
    }
};

} // namespace sim::Entities

#endif // SIM_ENTITIES_PLAYER_H_
