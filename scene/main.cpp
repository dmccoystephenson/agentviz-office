// agentviz-office: Claude Code sessions as an open-plan office floor.
//
// The page (web/office-*.js) owns the data and the layout: it reads the agent
// feed, places pods, desks and people, eases them between snapshots and walks
// people along aisles. Every frame it packs the scene in the agentviz wire
// format (vendor/agentviz-kit, docs/KIT.md in the agentviz repo) and calls
// agentviz_frame(); this file only draws what the frame says:
//   scene floats  the room: walls, corridor, the owner's office (room.h)
//   zones         carpets: pods (one per place) and session blocks (room.h)
//   agents        one desk and one person per session or subagent (desk.h, person.h)
//   links         floor trails: waiting (desk -> owner's door), delegation (trails.h)
#define STRATA_KIT_MAIN
#include "strata_kit.h"

#include "person.h"
#include "room.h"
#include "trails.h"

namespace {

agentviz::Frame frame;

}  // namespace

AGENTVIZ_FRAME_EXPORT(frame)

void strata_kit_configure(strata_kit::Options& o) {
    o.name = "agentviz-office";
    o.farPlane = 500.0f;
}

void strata_kit_engine_ready(strata_kit::Host&) {}

void strata_kit_build(strata_kit::Host& h, double seconds) {
    if (!frame.ok) return;
    const float t = static_cast<float>(seconds);
    const office::Floor fl = office::floorOf(frame);
    const float night = fl.nightForce >= 0.0f ? agentviz::clamp01(fl.nightForce) : h.night;
    // Indoors stays readable at night: a cool ambient lift, the lamps do the rest.
    h.ambientLift = {0.09f * night, 0.10f * night, 0.14f * night};
    office::drawRoom(h, fl, night);
    office::drawOwnerOffice(h, fl, t);
    office::drawZones(h, frame.zones, t);
    for (const auto& agent : frame.agents) {
        const office::Desk desk = office::deskOf(agent);
        office::drawDesk(h, desk, t, night);
        office::drawPerson(h, desk, t, night);
    }
    office::drawLinks(h, frame.links, t);
}
