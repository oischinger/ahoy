#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/hal.h"
#include "hoymiles.h"

namespace esphome {
namespace hoymiles {

    class HoymilesSensor : public sensor::Sensor, public PollingComponent, public hoymiles::HoymilesDevice {
        public:
            void setup() override;
            void update() override;
            void loop() override;
            void dump_config() override;

            float get_setup_priority() const override { return setup_priority::LATE; }

            void set_inverter_id(char *inverterId) { this->inviter_id_ = inverterId; }

            // void set_component_source(const char * 	source) {}
            void set_dc_voltage_sensor(int channel, sensor::Sensor *obj) { this->dc_channels_[channel].voltage_sensor_ = obj; }
            void set_dc_current_sensor(int channel, sensor::Sensor *obj) { this->dc_channels_[channel].current_sensor_ = obj; }
            void set_dc_power_sensor(int channel, sensor::Sensor *obj) { this->dc_channels_[channel].power_sensor_ = obj; }

            void set_grid_voltage_sensor(sensor::Sensor *obj) { this->grid_.voltage_sensor_ = obj; }
            void set_grid_current_sensor(sensor::Sensor *obj) { this->grid_.current_sensor_ = obj; }
            void set_grid_power_sensor(sensor::Sensor *obj) { this->grid_.power_sensor_ = obj; }
            void set_grid_frequency_sensor(sensor::Sensor *obj) { this->grid_.frequency_sensor_ = obj; }

            void set_general_yield_day_sensor(sensor::Sensor *obj) { this->general_.yield_day_sensor_ = obj; }
            void set_general_yield_total_sensor(sensor::Sensor *obj) { this->general_.yield_total_sensor_ = obj; }
            void set_general_temperatur_sensor(sensor::Sensor *obj) { this->general_.yield_temperature_sensor_ = obj; }

        protected:
            char *inviter_id_;

            struct HoymilesDCChannel {
                sensor::Sensor *voltage_sensor_{nullptr};
                sensor::Sensor *current_sensor_{nullptr};
                sensor::Sensor *power_sensor_{nullptr};
            } dc_channels_[6];

            struct HoymilesGrid {
                sensor::Sensor *voltage_sensor_{nullptr};
                sensor::Sensor *current_sensor_{nullptr};
                sensor::Sensor *power_sensor_{nullptr};
                sensor::Sensor *frequency_sensor_{nullptr};
            } grid_;    

            struct HoymilesGeneral {
                sensor::Sensor *yield_total_sensor_{nullptr};
                sensor::Sensor *yield_day_sensor_{nullptr};
                sensor::Sensor *yield_temperature_sensor_{nullptr};
            } general_;        

    };

}  // namespace uart
}  // namespace esphome