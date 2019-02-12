#include "Events/DMActionEvents.hpp"
#include "API/CAppManager.hpp"
#include "API/CServerExoApp.hpp"
#include "API/CGameObjectArray.hpp"
#include "API/Functions.hpp"
#include "API/CNWSPlayer.hpp"
#include "API/CNWSMessage.hpp"
#include "API/Types.hpp"
#include "API/Constants.hpp"
#include "API/Globals.hpp"
#include "Events.hpp"
#include "Services/Patching/Patching.hpp"
#include "Utils.hpp"


namespace Events {

using namespace NWNXLib;
using namespace NWNXLib::API;
using namespace NWNXLib::API::Constants;
using namespace NWNXLib::Platform;

static NWNXLib::Hooking::FunctionHook* m_HandlePlayerToServerDungeonMasterMessageHook = nullptr;

DMActionEvents::DMActionEvents(NWNXLib::ViewPtr<NWNXLib::Services::HooksProxy> hooker)
{
    hooker->RequestExclusiveHook<Functions::CNWSMessage__HandlePlayerToServerDungeonMasterMessage>(&HandleDMMessageHook);

    m_HandlePlayerToServerDungeonMasterMessageHook = hooker->FindHookByAddress(Functions::CNWSMessage__HandlePlayerToServerDungeonMasterMessage);
}
template <typename T>
static T PeekMessage(CNWSMessage *pMessage, int32_t offset)
{
    static_assert(std::is_pod<T>::value);
    T value;
    uint8_t *ptr = pMessage->m_pnReadBuffer + pMessage->m_nReadBufferPtr + offset;
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

int32_t HandleGiveEvent(CNWSMessage *pMessage, CNWSPlayer *pPlayer, uint8_t nMinor, int32_t bGroup,
                        const std::string &event, int32_t alignmentType = 0)
{
    int32_t retVal;
    Types::ObjectID oidDM = pPlayer ? pPlayer->m_oidNWSObject : OBJECT_INVALID;
    std::string amount = std::to_string(PeekMessage<int32_t>(pMessage, 0));
    std::string target = Utils::ObjectIDToString(PeekMessage<Types::ObjectID>(pMessage, 4) & 0x7FFFFFFF);

    auto PushAndSignalGiveEvent = [&](std::string ev) -> bool {
        Events::PushEventData("AREA", amount);
        Events::PushEventData("OBJECT", target);
        if (alignmentType > 0)
        {
            Events::PushEventData("ALIGNMENT_TYPE", std::to_string(alignmentType));
        }
        return Events::SignalEvent(ev, oidDM);
    };

    if (PushAndSignalGiveEvent(event + "_BEFORE"))
    {
        retVal = m_HandlePlayerToServerDungeonMasterMessageHook->CallOriginal<int32_t>(pMessage, pPlayer, nMinor, bGroup);
    }
    else
    {
        retVal = false;
    }

    PushAndSignalGiveEvent(event + "_AFTER");

    return retVal;
}

int32_t HandleGroupEvent(CNWSMessage *pMessage, CNWSPlayer *pPlayer, uint8_t nMinor, int32_t bGroup, const std::string &event)
{
    int32_t retVal;
    Types::ObjectID oidDM = pPlayer ? pPlayer->m_oidNWSObject : OBJECT_INVALID;
    int32_t offset = 0;
    int32_t groupSize = 1;

    if (bGroup)
    {
        groupSize = PeekMessage<int32_t>(pMessage, offset);
        offset += sizeof(groupSize);
    }

    Types::ObjectID targets[groupSize];

    for (int32_t target = 0; target < groupSize; target++)
    {
        targets[target] = PeekMessage<Types::ObjectID>(pMessage, offset) & 0x7FFFFFFF;
        offset += sizeof(targets[target]);
    }

    auto PushAndSignalGroupEvent = [&](std::string ev) -> bool {
        Events::PushEventData("NUM_TARGETS", std::to_string(groupSize));
        for(int32_t target = 0; target < groupSize; target++)
        {
            Events::PushEventData("TARGET_" + std::to_string(target + 1), Utils::ObjectIDToString(targets[target]));
        }
        return Events::SignalEvent(ev, oidDM);
    };

    if (PushAndSignalGroupEvent(event + "_BEFORE"))
    {
        retVal = m_HandlePlayerToServerDungeonMasterMessageHook->CallOriginal<int32_t>(pMessage, pPlayer, nMinor, bGroup);
    }
    else
    {
        retVal = false;
    }

    PushAndSignalGroupEvent(event + "_AFTER");

    return retVal;
}

int32_t DMActionEvents::HandleDMMessageHook(CNWSMessage *thisPtr, CNWSPlayer *pPlayer, uint8_t nMinor, int32_t bGroup)
{
    int32_t retVal;
    std::string event = "NWNX_ON_DM_";
    Types::ObjectID oidDM = pPlayer ? pPlayer->m_oidNWSObject : OBJECT_INVALID;

    auto DefaultSignalEvent = [&]() -> void {
        if (Events::SignalEvent(event + "_BEFORE", oidDM))
        {
            retVal = m_HandlePlayerToServerDungeonMasterMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor, bGroup);
        }
        else
        {
            retVal  = false;
        }
        Events::SignalEvent(event + "_AFTER", oidDM);
    };

    switch (nMinor)
    {
        case MessageDungeonMasterMinor::SpawnCreature:
        case MessageDungeonMasterMinor::SpawnItem:
        case MessageDungeonMasterMinor::SpawnPlaceable:
        case MessageDungeonMasterMinor::SpawnWaypoint:
        case MessageDungeonMasterMinor::SpawnTrigger:
        case MessageDungeonMasterMinor::SpawnEncounter:
        case MessageDungeonMasterMinor::SpawnPortal:
        {
            event += "SPAWN_OBJECT";

            std::string area = Utils::ObjectIDToString(PeekMessage<Types::ObjectID>(thisPtr, 0) & 0x7FFFFFFF);
            std::string object = Utils::ObjectIDToString(Globals::AppManager()->m_pServerExoApp->GetObjectArray()->m_nNextObjectArrayID[0]);
            int32_t objectType;
            std::string x = std::to_string(PeekMessage<float>(thisPtr, 4));
            std::string y = std::to_string(PeekMessage<float>(thisPtr, 8));
            std::string z = std::to_string(PeekMessage<float>(thisPtr, 12));

            switch (nMinor)
            {
                case MessageDungeonMasterMinor::SpawnCreature:
                    objectType = ObjectType::Creature;
                    break;
                case MessageDungeonMasterMinor::SpawnItem:
                    objectType = ObjectType::Item;
                    break;
                case MessageDungeonMasterMinor::SpawnPlaceable:
                    objectType = ObjectType::Placeable;
                    break;
                case MessageDungeonMasterMinor::SpawnWaypoint:
                    objectType = ObjectType::Waypoint;
                    break;
                case MessageDungeonMasterMinor::SpawnTrigger:
                    objectType = ObjectType::Trigger;
                    break;
                case MessageDungeonMasterMinor::SpawnEncounter:
                    objectType = ObjectType::Encounter;
                    break;
                case MessageDungeonMasterMinor::SpawnPortal:
                    objectType = ObjectType::Portal;
                    break;

                default:
                    break;
            }

            auto PushAndSignal = [&](std::string ev) -> bool {
                Events::PushEventData("AREA", area);
                Events::PushEventData("OBJECT", object);
                Events::PushEventData("OBJECT_TYPE", std::to_string(objectType));
                Events::PushEventData("POS_X", x);
                Events::PushEventData("POS_Y", y);
                Events::PushEventData("POS_Z", z);
                return Events::SignalEvent(ev, oidDM);
            };

            if (PushAndSignal(event + "_BEFORE"))
            {
                retVal = m_HandlePlayerToServerDungeonMasterMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor, bGroup);
            }
            else
            {
                retVal = false;
            }

            PushAndSignal(event +"_AFTER");
            break;
        }
        case MessageDungeonMasterMinor::Difficulty:
        {
            event += "CHANGE_DIFFICULTY";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::ViewInventory:
        {
            event += "VIEW_INVENTORY";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::SpawnTrapOnObject:
        {
            event += "SPAWN_TRAP_ON_OBJECT";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::Heal:
        {
            event += "HEAL";
            retVal = HandleGroupEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::Kill:
        {
            event += "KILL";
            retVal = HandleGroupEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::Goto:
        {
            event += "JUMP";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::Possess:
        {
            event += "POSSESS";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::Invulnerable:
        {
            event += "TOGGLE_INVULNERABLE";
            retVal = HandleGroupEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::Rest:
        {
            event += "FORCE_REST";
            retVal = HandleGroupEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::Limbo:
        {
            event += "LIMBO";
            retVal = HandleGroupEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::ToggleAI:
        {
            event += "TOGGLE_AI";
            retVal = HandleGroupEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::ToggleLock:
        {
            event += "TOGGLE_LOCK";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::DisableTrap:
        {
            event += "DISABLE_TRAP";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::Manifest:
        {
            event += "APPEAR";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::Unmanifest:
        {
            event += "DISAPPEAR";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::Immortal:
        {
            event += "TOGGLE_IMMORTAL";
            retVal = HandleGroupEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::GotoPoint:
        {
            event += "JUMP_TO_POINT";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::GiveXP:
        {
            event += "GIVE_XP";
            retVal = HandleGiveEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::GiveLevel:
        {
            event += "GIVE_LEVEL";
            retVal = HandleGiveEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::GiveGold:
        {
            event += "GIVE_GOLD";
            retVal = HandleGiveEvent(thisPtr, pPlayer, nMinor, bGroup, event);
            break;
        }
        case MessageDungeonMasterMinor::SetFaction:
        case MessageDungeonMasterMinor::SetFactionByName:
        {
            event += "SET_FACTION";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::GiveItem:
        {
            event += "GIVE_ITEM";

            std::string target = Utils::ObjectIDToString(PeekMessage<Types::ObjectID>(thisPtr, 0) & 0x7FFFFFFF);
            std::string item = Utils::ObjectIDToString(Globals::AppManager()->m_pServerExoApp->GetObjectArray()->m_nNextObjectArrayID[0]);

            auto PushAndSignal = [&](std::string ev) -> bool {
                Events::PushEventData("TARGET", target);
                Events::PushEventData("ITEM", item);
                return Events::SignalEvent(ev, oidDM);
            };

            if (PushAndSignal(event + "_BEFORE"))
            {
                retVal = m_HandlePlayerToServerDungeonMasterMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor, bGroup);
            }
            else
            {
                retVal = false;
            }

            PushAndSignal(event +"_AFTER");
            break;
        }
        case MessageDungeonMasterMinor::TakeItem:
        {
            event += "TAKE_ITEM";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::GotoPointTarget:
        {
            event += "JUMP_TARGET_TO_POINT";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::GotoPointAllPlayers:
        {
            event += "JUMP_ALL_PLAYERS_TO_POINT";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::SetStat:
        {
            event += "SET_STAT";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::GetVar:
        {
            event += "GET_VARIABLE";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::SetVar:
        {
            event += "SET_VARIABLE";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::SetTime:
        {
            event += "SET_TIME";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::SetDate:
        {
            event += "SET_DATE";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::SetFactionReputation:
        {
            event += "SET_FACTION_REPUTATION";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::GetFactionReputation:
        {
            event += "GET_FACTION_REPUTATION";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::DumpLocals:
        {
            event += "DUMP_LOCALS";
            DefaultSignalEvent();
            break;
        }
        case MessageDungeonMasterMinor::GiveGoodAlignment:
        case MessageDungeonMasterMinor::GiveEvilAlignment:
        case MessageDungeonMasterMinor::GiveLawfulAlignment:
        case MessageDungeonMasterMinor::GiveChaoticAlignment:
        {
            event += "GIVE_ALIGNMENT";

            int32_t alignmentType = 0;
            switch (nMinor)
            {
                case MessageDungeonMasterMinor::GiveGoodAlignment:
                    alignmentType = Alignment::Good;
                    break;
                case MessageDungeonMasterMinor::GiveEvilAlignment:
                    alignmentType = Alignment::Evil;
                    break;
                case MessageDungeonMasterMinor::GiveLawfulAlignment:
                    alignmentType = Alignment::Lawful;
                    break;
                case MessageDungeonMasterMinor::GiveChaoticAlignment:
                    alignmentType = Alignment::Chaotic;
                    break;

                default:
                    break;
            }

            retVal = HandleGiveEvent(thisPtr, pPlayer, nMinor, bGroup, event, alignmentType);
            break;
        }
        default:
            retVal = m_HandlePlayerToServerDungeonMasterMessageHook->CallOriginal<int32_t>(thisPtr, pPlayer, nMinor, bGroup);
            break;
    }

    return retVal;
}

}
