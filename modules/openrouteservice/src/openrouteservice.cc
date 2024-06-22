#include "motis/openrouteservice/openrouteservice.h"

#include <cista/hash.h>
#include <filesystem>

#include "motis/core/common/logging.h"
#include "motis/module/context/motis_http_req.h"
#include "motis/module/event_collector.h"

#include "utl/to_vec.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace mm = motis::module;
namespace fs = std::filesystem;
namespace nc = net::http::client;
namespace rj = rapidjson;
namespace mp = motis::ppr;

namespace motis::openrouteservice {

openrouteservice::openrouteservice()
    : module("Openrouteservice Options", "openrouteservice") {
  param(url_, "url", "ORS API endpoint");
  param(api_key_, "api_key", "ORS API key");
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

  reg.register_op("/osrm/one_to_many",
                  [&](mm::msg_ptr const& msg) { return one_to_many(msg); }, {});

  reg.register_op("/osrm/via", [&](mm::msg_ptr const& msg) { return via(msg); },
                  {});
  reg.register_op("/ppr/route",
                  [&](mm::msg_ptr const& msg) { return ppr(msg); }, {});
  reg.register_op("/ppr/profiles",
                  [&](mm::msg_ptr const&) { return ppr_profiles(); }, {});
}

std::string_view translate_mode(std::string_view s) {
  switch (cista::hash(s)) {
    case cista::hash("foot"): return "foot-walking";
    case cista::hash("bike"): return "cycling-regular";
    case cista::hash("car"): return "driving-car";
    default: return "foot-walking";
  }
}

std::string json_to_string(rj::Document& doc) {
  rj::StringBuffer buffer;
  rj::Writer<rj::StringBuffer> writer(buffer);
  // Traverse the DOM and generates Handler events
  doc.Accept(writer);

  return {buffer.GetString(), buffer.GetSize()};
}

rj::Value encode_position(Position const* to, rj::Document& doc) {
  auto coord = rj::Value{rj::kArrayType};
  rj::Document::AllocatorType& allocator = doc.GetAllocator();

  coord.PushBack(rj::Value{to->lng()}, allocator);
  coord.PushBack(rj::Value{to->lat()}, allocator);

  return coord;
}

void construct_body(rj::Document& doc, rj::Value& locations, rj::Value& sources,
                    rj::Value& destinations) {
  doc.AddMember("locations", locations, doc.GetAllocator());
  doc.AddMember("sources", sources, doc.GetAllocator());
  doc.AddMember("destinations", destinations, doc.GetAllocator());

  auto metrics = rj::Value{rj::kArrayType};
  metrics.PushBack("distance", doc.GetAllocator());
  metrics.PushBack("duration", doc.GetAllocator());
  doc.AddMember("metrics", metrics, doc.GetAllocator());
}

std::string encode_body(osrm::OSRMOneToManyRequest const* req) {
  auto doc = rj::Document{};
  doc.SetObject();

  auto locations = rj::Value{rj::kArrayType};
  auto one = rj::Value{rj::kArrayType};
  auto many = rj::Value{rj::kArrayType};

  int index = 0;
  locations.PushBack(encode_position(req->one(), doc), doc.GetAllocator());
  one.PushBack(index++, doc.GetAllocator());
  for (auto const& to : *req->many()) {
    locations.PushBack(encode_position(to, doc), doc.GetAllocator());
    many.PushBack(index++, doc.GetAllocator());
  }

  if (req->direction() == SearchDir_Forward) {
    construct_body(doc, locations, one, many);
  } else {
    construct_body(doc, locations, many, one);
  }

  return json_to_string(doc);
}

std::string encode_body(osrm::OSRMManyToManyRequest const* req) {
  auto doc = rj::Document{};
  doc.SetObject();

  auto locations = rj::Value{rj::kArrayType};
  auto sources = rj::Value{rj::kArrayType};
  auto destinations = rj::Value{rj::kArrayType};

  int index = 0;
  for (auto const& from : *req->from()) {
    locations.PushBack(encode_position(from, doc), doc.GetAllocator());
    sources.PushBack(index++, doc.GetAllocator());
  }
  for (auto const& to : *req->to()) {
    locations.PushBack(encode_position(to, doc), doc.GetAllocator());
    destinations.PushBack(index++, doc.GetAllocator());
  }

  construct_body(doc, locations, sources, destinations);

  return json_to_string(doc);
}

template <typename Req>
mm::msg_ptr sources_to_targets(Req const* req, openrouteservice::impl* config) {
  auto const timer = motis::logging::scoped_timer{"openrouteservice.matrix"};

  // Encode OSRMOneToManyRequest as openrouteservice request
  auto body = encode_body(req);

  // Construct POST request
  auto const profile = (std::string)translate_mode(req->profile()->view());
  auto const url = config->url_ + "/matrix/" + profile;
  auto request = nc::request(url, nc::request::POST);
  request.headers["Authorization"] = config->api_key_;
  request.headers["Content-Type"] = "application/json; charset=utf-8";
  request.body = body;

  auto ors_request_start = std::chrono::system_clock::now();

  // Send request and parse the response as JSON
  auto f = motis_http(request);  // motis_http(query);
  auto v = f->val();
  // std::cout << "ORS response: " << v.body << std::endl;

  rapidjson::Document doc;
  if (doc.Parse(v.body.data(), v.body.size()).HasParseError()) {
    doc.GetParseError();
    throw utl::fail("ORS response: Bad JSON: {} at offset {}",
                    rapidjson::GetParseError_En(doc.GetParseError()),
                    doc.GetErrorOffset());
  }

  // variable Lto store the request time
  auto ors_request_finished = std::chrono::system_clock::now();
  auto ore_request_elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(ors_request_finished - ors_request_start);
  // log request time
  LOG(logging::info) << "ORS Matrix request time: " << ore_request_elapsed_seconds.count() << " s";

  std::vector<double> distances;
  std::vector<double> durations;

  // Check if v.status_code is != 200 if the length of the request body "locations" is 1
  if (v.status_code != 200) {
    // Request doc
    rapidjson::Document doc_request;
    doc_request.Parse(request.body.data(), request.body.size());
    const rapidjson::Value& locations = doc_request["locations"];
    // Error log the request body
    LOG(logging::warn) << ">>ORS error<<";
    LOG(logging::warn) << "Request status code: " << v.status_code;
    LOG(logging::warn) << "Request url: " << url;
    LOG(logging::warn) << "Respone body: " << v.body;
    LOG(logging::warn) << "Request body parse size: " << locations.GetArray().Size();
    LOG(logging::warn) << "Request body: " << body;
    if (locations.GetArray().Size() == 1) {
      LOG(logging::warn) << "Manually emplacing 0 for distance and duration";
      // emplace back distances and durations with 0
      distances.emplace_back(0);
      durations.emplace_back(0);
    } else {
      throw utl::fail("ORS response: Bad status code: {}", v.status_code);
    }
  } else {
    // Extract distances and durations
    if (doc["distances"].Size() == doc["durations"].Size()) {
      for (rj::SizeType i = 0; i < doc["distances"].Size(); i++) {
        if (doc["distances"][i].Size() == doc["durations"][i].Size()) {
          for (const rj::Value& distance : doc["distances"][i].GetArray()) {
            distances.emplace_back(distance.GetDouble());
          }
          for (const rj::Value& duration : doc["durations"][i].GetArray()) {
            durations.emplace_back(duration.GetDouble());
          }
        } else {
          throw utl::fail("Dimensions of distance/duration matrices don't match");
        }
      }
    } else {
      throw utl::fail("Dimensions of distance/duration matrices don't match");
    }

  }


  // Encode openrouteservice response.
  std::vector<osrm::Cost> costs;
  for (int i = 0; i < durations.size(); i++) {
    costs.emplace_back(durations[i], distances[i]);
  }
  // Log the pure motis duration
  auto motis_request_start = std::chrono::system_clock::now();

  mm::message_creator fbb;
  fbb.create_and_finish(
      MsgContent_OSRMOneToManyResponse,
      CreateOSRMOneToManyResponse(fbb, fbb.CreateVectorOfStructs(costs))
          .Union());
  // Measure motis time
  auto motis_request_finished = std::chrono::system_clock::now();
  auto motis_request_elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(motis_request_finished - motis_request_start);;
  // log motis time
  LOG(logging::info) << "Motis processing time: " << motis_request_elapsed_seconds.count() << " s";
  // Log total time
  return make_msg(fbb);
}

mm::msg_ptr openrouteservice::table(mm::msg_ptr const& msg) const {
  using osrm::OSRMManyToManyRequest;
  auto const req = motis_content(OSRMManyToManyRequest, msg);
  return sources_to_targets(req, config.get());
}

mm::msg_ptr openrouteservice::one_to_many(mm::msg_ptr const& msg) const {
  using osrm::OSRMOneToManyRequest;
  auto const req = motis_content(OSRMOneToManyRequest, msg);
  return sources_to_targets(req, config.get());
}

std::string encode_body(osrm::OSRMViaRouteRequest const* req) {
  auto doc = rj::Document{};
  doc.SetObject();

  auto coordinates = rj::Value{rj::kArrayType};
  for (auto const& to : *req->waypoints()) {
    coordinates.PushBack(encode_position(to, doc), doc.GetAllocator());
  }
  doc.AddMember("coordinates", coordinates, doc.GetAllocator());

  return json_to_string(doc);
}

mm::msg_ptr openrouteservice::via(mm::msg_ptr const& msg) const {
  using osrm::OSRMViaRouteRequest;
  auto const req = motis_content(OSRMViaRouteRequest, msg);

  // Encode OSRMViaRouteRequest as openrouteservice request
  auto body = encode_body(req);

  // Construct POST request
  auto const profile = (std::string)translate_mode(req->profile()->view());
  auto const url = url_ + "/directions/" + profile + "/geojson";
  auto request = nc::request(url, nc::request::POST);
  request.headers["Authorization"] = api_key_;
  request.headers["Content-Type"] = "application/json; charset=utf-8";
  request.body = body;
  // variable to store the request time
  auto ors_request_start = std::chrono::system_clock::now();

  // Send request and parse the response as JSON
  auto f = motis_http(request);  // motis_http(query);
  auto v = f->val();
  auto ors_request_finished = std::chrono::system_clock::now();
  auto ore_request_elapsed_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(ors_request_finished -
                                                       ors_request_start);
  // log request time
  LOG(logging::info) << "ORS 'VIA' request time: " << ore_request_elapsed_seconds.count() << " s";

  rapidjson::Document doc;
  if (doc.Parse(v.body.data(), v.body.size()).HasParseError()) {
    doc.GetParseError();
    throw utl::fail("ORS response: Bad JSON: {} at offset {}",
                    rapidjson::GetParseError_En(doc.GetParseError()),
                    doc.GetErrorOffset());
  }
  const rapidjson::Value& feature = doc["features"][0];

  // Get totals
  const rapidjson::Value& summary = feature["properties"]["summary"];
  double duration = summary["duration"].GetDouble();
  double distance = summary["distance"].GetDouble();

  // Extract route geometry
  std::vector<double> coordinates;
  for (const rapidjson::Value& coordinate_pair :
       feature["geometry"]["coordinates"].GetArray()) {
    coordinates.emplace_back(coordinate_pair[1].GetDouble());
    coordinates.emplace_back(coordinate_pair[0].GetDouble());
  }

  // Encode OSRM response
  mm::message_creator fbb;
  fbb.create_and_finish(
      MsgContent_OSRMViaRouteResponse,
      osrm::CreateOSRMViaRouteResponse(
          fbb, static_cast<int>(duration), static_cast<double>(distance),
          CreatePolyline(
              fbb, fbb.CreateVector(coordinates.data(), coordinates.size())))
          .Union());
  return make_msg(fbb);
}

mm::msg_ptr openrouteservice::ppr(mm::msg_ptr const& msg) const {
  using osrm::OSRMOneToManyResponse;
  using osrm::OSRMViaRouteResponse;
  using ppr::FootRoutingRequest;
  using ppr::FootRoutingResponse;

  auto const req = motis_content(FootRoutingRequest, msg);
  mm::message_creator fbb;
  if (req->include_path()) {
    fbb.create_and_finish(
        MsgContent_FootRoutingResponse,
        ppr::CreateFootRoutingResponse(
            fbb,
            fbb.CreateVector(utl::to_vec(
                *req->destinations(),
                [&](Position const* dest) {
                  mm::message_creator req_fbb;
                  auto const from_to = std::array<Position, 2>{
                      // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
                      req->search_direction() == SearchDir_Forward
                          ? *req->start()
                          : *dest,
                      // NOLINTNEXTLINE(clang-analyzer-core.NonNullParamChecker)
                      req->search_direction() == SearchDir_Forward
                          ? *dest
                          : *req->start()};
                  req_fbb.create_and_finish(
                      MsgContent_OSRMViaRouteRequest,
                      osrm::CreateOSRMViaRouteRequest(
                          req_fbb, req_fbb.CreateString("foot"),
                          req_fbb.CreateVectorOfStructs(from_to.data(), 2U))
                          .Union());
                  auto const res_msg = via(make_msg(req_fbb));
                  auto const res = motis_content(OSRMViaRouteResponse, res_msg);
                  return ppr::CreateRoutes(
                      fbb,
                      fbb.CreateVector(std::vector{ppr::CreateRoute(
                          fbb, res->distance(), res->time()/60.0, res->time()/60.0, 0.0,
                          0U, 0.0, 0.0, req->start(), dest,
                          fbb.CreateVector(
                              std::vector<
                                  flatbuffers::Offset<ppr::RouteStep>>{}),
                          fbb.CreateVector(
                              std::vector<flatbuffers::Offset<ppr::Edge>>{}),
                          motis_copy_table(Polyline, fbb, res->polyline()), 0,
                          0)}));
                })))
            .Union());
  } else {
    mm::message_creator req_fbb;
    auto const start = *req->start();
    auto const dests =
        utl::to_vec(*req->destinations(), [](Position const* dest) {
          return Position{dest->lat(), dest->lng()};
        });
    req_fbb.create_and_finish(
        MsgContent_OSRMOneToManyRequest,
        osrm::CreateOSRMOneToManyRequest(
            req_fbb, req_fbb.CreateString("foot"), req->search_direction(),
            &start, req_fbb.CreateVectorOfStructs(dests.data(), dests.size()))
            .Union(),
        "/osrm/one_to_many");
    auto const res_msg = one_to_many(make_msg(req_fbb));
    auto const res = motis_content(OSRMOneToManyResponse, res_msg);
    fbb.create_and_finish(
        MsgContent_FootRoutingResponse,
        ppr::CreateFootRoutingResponse(
            fbb,
            fbb.CreateVector(utl::to_vec(
                *res->costs(),
                [&, i = 0](osrm::Cost const* cost) mutable {
                  // divide cost by 60 to get time in minutes
                  auto const vec = std::vector{ppr::CreateRoute(
                      fbb, cost->distance(), cost->duration()/60.0, cost->duration()/60.0,
                      0.0, 0U, 0.0, 0.0, req->start(),
                      req->destinations()->Get(i++),
                      fbb.CreateVector(
                          std::vector<flatbuffers::Offset<ppr::RouteStep>>{}),
                      fbb.CreateVector(
                          std::vector<flatbuffers::Offset<ppr::Edge>>{}),
                      CreatePolyline(fbb,
                                     fbb.CreateVector(std::vector<double>{})))};
                  return ppr::CreateRoutes(fbb, fbb.CreateVector(vec));
                })))
            .Union());
  }
  return make_msg(fbb);
}

mm::msg_ptr openrouteservice::ppr_profiles() const {
  mm::message_creator fbb;
  std::map<std::string, double> profile{{"foot_ors", 5/3.6}};
  auto profiles = utl::to_vec(profile, [&](auto const& e) {
    return mp::CreateFootRoutingProfileInfo(fbb, fbb.CreateString(e.first),
                                            e.second);
  });
  fbb.create_and_finish(MsgContent_FootRoutingProfilesResponse,
                        CreateFootRoutingProfilesResponse(
                            fbb, fbb.CreateVectorOfSortedTables(&profiles))
                            .Union());
  return make_msg(fbb);
}

}  // namespace motis::openrouteservice