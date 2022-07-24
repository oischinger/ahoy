#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "../hoymiles.h"

namespace esphome {
namespace hoymiles {

class HoymilesButton : public button::Button, public Component, public hoymiles::HoymilesDevice {
  public:
    void dump_config() override;
    void set_inverter_id(char *inverterId) { this->inviter_id_ = inverterId; }

  protected:
    char *inviter_id_;

    void press_action() override;
};

}  // namespace hoymiles
}  // namespace esphome