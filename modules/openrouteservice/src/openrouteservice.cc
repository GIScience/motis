#include "motis/openrouteservice/openrouteservice.h"

#include <filesystem>

#include "motis/core/common/logging.h"
#include "motis/module/event_collector.h"

using namespace motis::module;
using namespace motis::logging;

namespace motis::openrouteservice {

openrouteservice::openrouteservice(): module("Openrouteservice Options", "openrouteservice") {
  param(url_, "url", "ORS API endpoint");
}

openrouteservice::~openrouteservice() = default;

void openrouteservice::init(motis::module::registry& reg) {
  std::cout << "Servus ORS!";
  /*
  reg.subscribe("/init", [this] { init_async(); }, {});
  reg.register_op("/osrm/table",
                  [this](msg_ptr const& msg) {
                    auto const req = motis_content(OSRMManyToManyRequest, msg);
                    return get_router(req->profile()->str())->table(req);
                  },
                  {});
  reg.register_op("/osrm/one_to_many",
                  [this](msg_ptr const& msg) {
                    auto const req = motis_content(OSRMOneToManyRequest, msg);
                    return get_router(req->profile()->str())->one_to_many(req);
                  },
                  {});
  reg.register_op("/osrm/via",
                  [this](msg_ptr const& msg) {
                    auto const req = motis_content(OSRMViaRouteRequest, msg);
                    return get_router(req->profile()->str())->via(req);
                  },
                  {});
  reg.register_op("/osrm/smooth_via",
                  [this](msg_ptr const& msg) {
                    auto const req =
                        motis_content(OSRMSmoothViaRouteRequest, msg);
                    return get_router(req->profile()->str())->smooth_via(req);
                  },
                  {});
                  */
}

}  // namespace motis::osrm