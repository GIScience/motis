#pragma once

#include "motis/module/module.h"

namespace motis::openrouteservice {

struct openrouteservice : public motis::module::module {
public:
  openrouteservice();
  ~openrouteservice() override;

//  openrouteservice(openrouteservice const&) = delete;
//  openrouteservice& operator=(openrouteservice const&) = delete;
//
//  openrouteservice(openrouteservice&&) = delete;
//  openrouteservice& operator=(openrouteservice&&) = delete;
//
  motis::module::msg_ptr table(motis::module::msg_ptr const&) const;

  motis::module::msg_ptr one_to_many(motis::module::msg_ptr const&) const;

  motis::module::msg_ptr via(motis::module::msg_ptr const&) const;

  // create a struct that holds the url and api key
  struct impl {
    explicit impl(std::string const& url, std::string const& api_key)
        : url_(url), api_key_(api_key) {}
    std::string url_;
    std::string api_key_;
  };
  std::unique_ptr<impl> config;

  void init(motis::module::registry&) override;

private:
  //struct impl;
  //std::unique_ptr<impl> impl_;

  std::string url_;
  std::string api_key_;

  std::vector<std::string> profiles_;
};

}  // namespace motis::openrouteservice
