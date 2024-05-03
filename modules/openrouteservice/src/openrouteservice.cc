#include "motis/openrouteservice/openrouteservice.h"

#include <cista/hash.h>
#include <filesystem>

#include "motis/core/common/logging.h"
#include "motis/module/event_collector.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "motis/module/context/motis_http_req.h"

namespace mm = motis::module;
namespace fs = std::filesystem;
namespace nc = net::http::client;
namespace rj = rapidjson;

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

  doc.AddMember("locations", locations, doc.GetAllocator());
  bool forward = req->direction() == SearchDir_Forward;
  doc.AddMember("sources", forward ? one : many, doc.GetAllocator());
  doc.AddMember("destinations", forward ? many : one, doc.GetAllocator());

  auto metrics = rj::Value{rj::kArrayType};
  metrics.PushBack("distance", doc.GetAllocator());
  metrics.PushBack("duration", doc.GetAllocator());
  doc.AddMember("metrics", metrics, doc.GetAllocator());

  return json_to_string(doc);
}

std::string encode_body(osrm::OSRMManyToManyRequest const* req) {
  auto doc = rj::Document{};
  doc.SetObject();

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

  // Send request and parse the response as JSON
  auto f = motis_http(request);  // motis_http(query);
  auto v = f->val();
  std::cout << "ORS response: " << v.body << std::endl;

  rapidjson::Document doc;
  if (doc.Parse(v.body.data(), v.body.size()).HasParseError()) {
    doc.GetParseError();
    throw utl::fail("ORS response: Bad JSON: {} at offset {}",
                    rapidjson::GetParseError_En(doc.GetParseError()),
                    doc.GetErrorOffset());
  }

  // Extract distances and durations
  std::vector<double> distances;
  std::vector<double> durations;
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

  // Encode OSRM response.
  std::vector<osrm::Cost> costs;
  for (int i = 0; i < durations.size(); i++) {
    costs.emplace_back(durations[i], distances[i]);
  }

  mm::message_creator fbb;
  fbb.create_and_finish(
      MsgContent_OSRMOneToManyResponse,
      CreateOSRMOneToManyResponse(fbb, fbb.CreateVectorOfStructs(costs))
          .Union());
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

  // Send request and parse the response as JSON
  auto f = motis_http(request);  // motis_http(query);
  auto v = f->val();
  std::cout << "ORS response: " << v.body << std::endl;

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

}  // namespace motis::openrouteservice