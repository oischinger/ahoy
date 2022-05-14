#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import struct
import re
import time
from datetime import datetime
import argparse
import hoymiles
from RF24 import RF24, RF24_PA_LOW, RF24_PA_MAX, RF24_250KBPS, RF24_CRC_DISABLED, RF24_CRC_8, RF24_CRC_16
import paho.mqtt.client
import yaml
from yaml.loader import SafeLoader

def main_loop():
    """Main loop"""
    inverters = [
            inverter for inverter in ahoy_config.get('inverters', [])
            if not inverter.get('disabled', False)]

    for inverter in inverters:
        if hoymiles.HOYMILES_DEBUG_LOGGING:
            print(f'Poll inverter {inverter["serial"]}')
        poll_inverter(inverter)

def poll_inverter(inverter, retries=4):
    """
    Send/Receive command_queue, initiate status poll on inverter

    :param str inverter: inverter serial
    :param retries: tx retry count if no inverter contact
    :type retries: int
    """
    inverter_ser = inverter.get('serial')
    dtu_ser = ahoy_config.get('dtu', {}).get('serial')

    # Queue at least status data request
    command_queue[str(inverter_ser)].append(hoymiles.compose_set_time_payload())

    # Putt all queued commands for current inverter on air
    while len(command_queue[str(inverter_ser)]) > 0:
        payload = command_queue[str(inverter_ser)].pop(0)

        # Send payload {ttl}-times until we get at least one reponse
        payload_ttl = retries
        while payload_ttl > 0:
            payload_ttl = payload_ttl - 1
            com = hoymiles.InverterTransaction(
                    radio=hmradio,
                    dtu_ser=dtu_ser,
                    inverter_ser=inverter_ser,
                    request=next(hoymiles.compose_esb_packet(
                        payload,
                        seq=b'\x80',
                        src=dtu_ser,
                        dst=inverter_ser
                        )))
            response = None
            while com.rxtx():
                try:
                    response = com.get_payload()
                    payload_ttl = 0
                except Exception as e:
                    print(f'Error while retrieving data: {e}')
                    pass

        # Handle the response data if any
        if response:
            dt = datetime.now()
            print(f'{dt} Payload: ' + hoymiles.hexify_payload(response))
            decoder = hoymiles.ResponseDecoder(response,
                    request=com.request,
                    inverter_ser=inverter_ser
                    )
            result = decoder.decode()
            if isinstance(result, hoymiles.decoders.StatusResponse):
                data = result.__dict__()
                if hoymiles.HOYMILES_DEBUG_LOGGING:
                    print(f'{dt} Decoded: {data["temperature"]}', end='')
                    phase_id = 0
                    for phase in data['phases']:
                        print(f' phase{phase_id}=voltage:{phase["voltage"]}, current:{phase["current"]}, power:{phase["power"]}, frequency:{data["frequency"]}', end='')
                        phase_id = phase_id + 1
                    string_id = 0
                    for string in data['strings']:
                        print(f' string{string_id}=voltage:{string["voltage"]}, current:{string["current"]}, power:{string["power"]}, total:{string["energy_total"]/1000}, daily:{string["energy_daily"]}', end='')
                        string_id = string_id + 1
                    print()

                if mqtt_client:
                    mqtt_send_status(mqtt_client, inverter_ser, data,
                            topic=inverter.get('mqtt', {}).get('topic', None))

def mqtt_send_status(broker, inverter_ser, data, topic=None):
    """
    Publish StatusResponse object

    :param paho.mqtt.client.Client broker: mqtt-client instance
    :param str inverter_ser: inverter serial
    :param hoymiles.StatusResponse data: decoded inverter StatusResponse
    :param topic: custom mqtt topic prefix (default: hoymiles/{inverter_ser})
    :type topic: str
    """

    if not topic:
        topic = f'hoymiles/{inverter_ser}'

    # AC Data
    phase_id = 0
    for phase in data['phases']:
        broker.publish(f'{topic}/emeter/{phase_id}/power', phase['power'])
        broker.publish(f'{topic}/emeter/{phase_id}/voltage', phase['voltage'])
        broker.publish(f'{topic}/emeter/{phase_id}/current', phase['current'])
        phase_id = phase_id + 1

    # DC Data
    string_id = 0
    for string in data['strings']:
        broker.publish(f'{topic}/emeter-dc/{string_id}/total', string['energy_total']/1000)
        broker.publish(f'{topic}/emeter-dc/{string_id}/power', string['power'])
        broker.publish(f'{topic}/emeter-dc/{string_id}/voltage', string['voltage'])
        broker.publish(f'{topic}/emeter-dc/{string_id}/current', string['current'])
        string_id = string_id + 1
    # Global
    broker.publish(f'{topic}/frequency', data['frequency'])
    broker.publish(f'{topic}/temperature', data['temperature'])

def mqtt_on_command(client, userdata, message):
    """
    Handle commands to topic
        hoymiles/{inverter_ser}/command
    frame a payload and put onto command_queue

    Inverters must have mqtt.send_raw_enabled: true configured

    This can be used to inject debug payloads
    The message must be in hexlified format

    Use of variables:
    tttttttt gets expanded to a current int(time)

    Example injects exactly the same as we normally use to poll data:
      mosquitto -h broker -t inverter_topic/command -m 800b00tttttttt0000000500000000

    This allows for even faster hacking during runtime

    :param paho.mqtt.client.Client client: mqtt-client instance
    :param dict userdata: Userdata
    :param dict message: mqtt-client message object
    """
    try:
        inverter_ser = next(
                item[0] for item in mqtt_command_topic_subs if item[1] == message.topic)
    except StopIteration:
        print('Unexpedtedly received mqtt message for {message.topic}')

    if inverter_ser:
        p_message = message.payload.decode('utf-8').lower()

        # Expand tttttttt to current time for use in hexlified payload
        expand_time = ''.join(f'{b:02x}' for b in struct.pack('>L', int(time.time())))
        p_message = p_message.replace('tttttttt', expand_time)

        if (len(p_message) < 2048 \
            and len(p_message) % 2 == 0 \
            and re.match(r'^[a-f0-9]+$', p_message)):
            payload = bytes.fromhex(p_message)
            # commands must start with \x80
            if payload[0] == 0x80:
                command_queue[str(inverter_ser)].append(
                    hoymiles.frame_payload(payload[1:]))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Ahoy - Hoymiles solar inverter gateway', prog="hoymiles")
    parser.add_argument("-c", "--config-file", nargs="?", required=True,
        help="configuration file")
    parser.add_argument("--log-transactions", action="store_true", default=False,
        help="Enable transaction logging output")
    parser.add_argument("--verbose", action="store_true", default=False,
        help="Enable debug output")
    global_config = parser.parse_args()

    # Load ahoy.yml config file
    try:
        if isinstance(global_config.config_file, str) == True:
            with open(global_config.config_file, 'r') as yf:
                cfg = yaml.load(yf, Loader=SafeLoader)
        else:
            with open('ahoy.yml', 'r') as yf:
                cfg = yaml.load(yf, Loader=SafeLoader)
    except FileNotFoundError:
        print("Could not load config file. Try --help")
        sys.exit(2)
    except yaml.YAMLError as ye:
        print('Failed to load config frile {global_config.config_file}: {ye}')
        sys.exit(1)

    ahoy_config = dict(cfg.get('ahoy', {}))

    # Prepare for multiple transceivers, makes them configurable (currently
    # only one supported)
    for radio_config in ahoy_config.get('nrf', [{}]):
        radio = RF24(
                radio_config.get('ce_pin', 22),
                radio_config.get('cs_pin', 0),
                radio_config.get('spispeed', 1000000))
        hmradio = hoymiles.HoymilesNRF(device=radio)

    mqtt_client = None

    command_queue = {}
    mqtt_command_topic_subs = []

    if global_config.log_transactions:
        hoymiles.HOYMILES_TRANSACTION_LOGGING=True
    if global_config.verbose:
        hoymiles.HOYMILES_DEBUG_LOGGING=True

    mqtt_config = ahoy_config.get('mqtt', [])
    if not mqtt_config.get('disabled', False):
        mqtt_client = paho.mqtt.client.Client()
        mqtt_client.username_pw_set(mqtt_config.get('user', None), mqtt_config.get('password', None))
        mqtt_client.connect(mqtt_config.get('host', '127.0.0.1'), mqtt_config.get('port', 1883))
        mqtt_client.loop_start()
        mqtt_client.on_message = mqtt_on_command

    if not radio.begin():
        raise RuntimeError('Can\'t open radio')

    inverters = [inverter.get('serial') for inverter in ahoy_config.get('inverters', [])]
    for inverter in ahoy_config.get('inverters', []):
        inverter_ser = inverter.get('serial')
        command_queue[str(inverter_ser)] = []

        #
        # Enables and subscribe inverter to mqtt /command-Topic
        #
        if mqtt_client and inverter.get('mqtt', {}).get('send_raw_enabled', False):
            topic_item = (
                    str(inverter_ser),
                    inverter.get('mqtt', {}).get('topic', f'hoymiles/{inverter_ser}') + '/command'
                    )
            mqtt_client.subscribe(topic_item[1])
            mqtt_command_topic_subs.append(topic_item)

    loop_interval = ahoy_config.get('interval', 1)
    try:
        while True:
            t_loop_start = time.time()

            main_loop()

            print('', end='', flush=True)

            if loop_interval > 0 and (time.time() - t_loop_start) < loop_interval:
                time.sleep(time.time() % loop_interval)

    except KeyboardInterrupt:
        radio.powerDown()
        sys.exit()