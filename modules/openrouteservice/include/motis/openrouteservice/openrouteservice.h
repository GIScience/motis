#pragma once

#include "motis/module/module.h"

namespace motis::openrouteservice {

struct openrouteservice : public motis::module::module {
public:
  openrouteservice();
  ~openrouteservice() override;

  void init(motis::module::registry&) override;

private:
  //struct impl;
  //std::unique_ptr<impl> impl_;

  std::string url_;
};

}  // namespace motis::openrouteservice
