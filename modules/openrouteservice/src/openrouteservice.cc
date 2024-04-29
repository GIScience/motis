#include "motis/openrouteservice/openrouteservice.h"

#include <cista/hash.h>
#include <filesystem>

#include "motis/core/common/logging.h"
#include "motis/module/event_collector.h"

namespace mm = motis::module;
namespace fs = std::filesystem;
namespace o = motis::openrouteservice;

namespace mm = motis::module;

namespace motis::openrouteservice {

openrouteservice::openrouteservice()
    : module("Openrouteservice Options", "openrouteservice") {
  param(url_, "url", "ORS API endpoint");
}

openrouteservice::~openrouteservice() = default;

void openrouteservice::init(motis::module::registry& reg) {
  std::cout << "URL: " << this->url_ << std::endl;
  std::cout << "API key: " << this->api_key_ << std::endl;
  // Store the url and api key in the impl struct
  config = std::make_unique<impl>(url_, api_key_);
  //  reg.subscribe("/init", [this] { init_async(); }, {});
  reg.register_op("/osrm/table",
                  [&](mm::msg_ptr const& msg) { return table(msg); }, {});

  reg.register_op("/osrm/one_to_many", [&](mm::msg_ptr const& msg) { return one_to_many(msg); }, {});
}

mm::msg_ptr openrouteservice::one_to_many(mm::msg_ptr const& msg) const {
  using osrm::OSRMOneToManyRequest;
  auto const req = motis_content(OSRMOneToManyRequest, msg);
  //TODO Encode message as ORS query, send to the API, and decode the result
  return mm::msg_ptr();
}

std::string_view translate_mode(std::string_view s) {
  switch (cista::hash(s)) {
    case cista::hash("foot"): return "foot-walking";
    case cista::hash("bike"): return "cycling-regular";
    case cista::hash("car"): return "driving-car";
    default: return "foot-walking";
  }
}

template <typename Req>
mm::msg_ptr sources_to_targets(Req const* req, openrouteservice::impl* config) {
  auto const timer = motis::logging::scoped_timer{"openrouteservice.matrix"};
//  auto& thor = config->get();
//
//  // Encode OSRMManyToManyRequest as valhalla request.
//  auto doc = j::Document{};
//  encode_request(req, doc);
//
//  // Decode request.
//  v::Api request;
//  v::from_json(doc, v::Options::sources_to_targets, request);
//  auto& options = *request.mutable_options();
//
//  // Get the costing method.
//  auto mode = v::sif::TravelMode::kMaxTravelMode;
//  auto const mode_costing = thor.factory_.CreateModeCosting(options, mode);
//
//  // Find path locations (loki) for sources and targets.
//  thor.loki_worker_.matrix(request);
//
//  // Run matrix algorithm.
//  auto const res = thor.matrix_.SourceToTarget(
//      options.sources(), options.targets(), *config->reader_, mode_costing,
//      mode, 4000000.0F);
//  thor.matrix_.clear();
//
//  // Encode OSRM response.
  mm::message_creator fbb;
//  fbb.create_and_finish(
//      MsgContent_OSRMOneToManyResponse,
//      CreateOSRMOneToManyResponse(fbb, fbb.CreateVectorOfStructs(utl::to_vec(
//                                           res,
//                                           [](v::thor::TimeDistance const& td) {
//                                             return motis::osrm::Cost{
//                                                 td.time / 60.0, 1.0 * td.dist};
//                                           })))
//          .Union());
  return make_msg(fbb);
}

mm::msg_ptr openrouteservice::table(mm::msg_ptr const& msg) const {
  using osrm::OSRMManyToManyRequest;
  auto const req = motis_content(OSRMManyToManyRequest, msg);
  return sources_to_targets(req, config.get());
}

}  // namespace motis::openrouteservice