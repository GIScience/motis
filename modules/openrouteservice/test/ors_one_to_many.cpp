//
// Created by jules on 30.04.24.
//
#include "motis/module/message.h"
#include "motis/openrouteservice/openrouteservice.h"
#include "gtest/gtest.h"

namespace ors = motis::openrouteservice;

// Create test function that defines the test with a variable containing json
// data
TEST(ors_one_to_many, test_ors_one_to_many) {
  // Define the json data
  const auto json = R"(
    {
      "destination": {
      "type": "Module",
      "target": "/osrm/one_to_many"
      },
      "content_type": "OSRMOneToManyRequest",
      "content": {
      "profile": "car",
      "direction": "Forward",
      "one": {
        "lat": 50.770296,
        "lng": 6.095398
      },
      "many":
        [
          {
            "lat": 50.769428,
            "lng": 6.09524
          },
          {
            "lat": 50.772699,
            "lng": 6.095814
          },
          {
            "lat": 50.771013,
            "lng": 6.091147
          },
          {
            "lat": 50.767478,
            "lng": 6.09362
          },
          {
            "lat": 50.768748,
            "lng": 6.090632
          },
          {
            "lat": 50.769441,
            "lng": 6.100922
          },
          {
            "lat": 50.767837,
            "lng": 6.09114
          },
          {
            "lat": 50.766478,
            "lng": 6.096241
          },
          {
            "lat": 50.772022,
            "lng": 6.088498
          },
          {
            "lat": 50.775708,
            "lng": 6.095625
          }
        ]
      },
      "id": 1
    }
  )";
  // create a msg_ptr from the json data
  auto msg = motis::module::make_msg(json);
  // create a openrouteservice object and call the function one_to_many with a
  // const pointer to the json data
  auto ors = ors::openrouteservice();
  ors.one_to_many(msg);
  // TODO implement the one_to_many function in openrouteservice.cc
}