#include "hoymiles_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hoymiles {

    static const char *const TAG = "hoymiles.button";

    void HoymilesButton::dump_config() { 
        LOG_BUTTON("", "Hoymiles Button", this); 
    }

    void HoymilesButton::press_action() {
        ESP_LOGI(TAG, "Restart ...");

        Inverter<> *iv = this->parent_->get_inverter(this->inviter_id_);

        this->parent_->sendRestartPacket(iv->radioId.u64);

    // Let MQTT settle a bit
    // delay(100);  // NOLINT
    }

}  // namespace hoymiles
}  // namespace esphome