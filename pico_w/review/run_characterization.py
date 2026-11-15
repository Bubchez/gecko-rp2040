#!/usr/bin/env python3
"""Offline review probes of the ORIGINAL code, extracted at run time.

These characterize defects, not acceptance tests for future firmware. No GPIO,
network, Arduino, or ESPHome runtime is used. Build products go under work/.
Requires Python 3 and g++ with AddressSanitizer/UndefinedBehaviorSanitizer.
"""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "work" / "review" / "characterization"


def function(source, signature):
    """Extract one definition up to the next top-level closing brace."""
    start = source.index(signature)
    end = source.index("\n}", start) + 2
    return source[start:end]


PRELUDE = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static uint32_t fake_now = 100;
uint32_t millis() { return fake_now; }
struct Entity { template<class T> void publish_state(T) {} };
struct Offsets { uint16_t realSetPointG = 275; };
static Offsets GECKO_LOG_OFFSETS_V50, GECKO_LOG_OFFSETS_V51;
struct GeckoSpa {
  Entity *connected_sensor_=nullptr, *config_version_sensor_=nullptr,
    *status_version_sensor_=nullptr, *spa_time_sensor_=nullptr,
    *program_select_=nullptr;
  bool connected_=false, arduino_ready_=false, clock_sync_pending_=false;
  uint8_t reset_retry_count_=0, config_version_=0, status_version_=0,
    program_id_=255, tx_pending_=0;
  uint32_t last_i2c_time_=0, clock_sync_due_=0;
  static constexpr uint32_t CLOCK_SYNC_DELAY_MS=10000;
  static constexpr uint16_t MIN_STATUS_MSG_LEN=120, DEFAULT_EMBEDDED_STATUS_LEN=162;
  uint8_t msg_buffer_[512]{};
  uint16_t msg_buffer_len_=0, status_msg_len_=0;
  Offsets *log_offsets_=&GECKO_LOG_OFFSETS_V51;
  unsigned parsed=0, tx_acks=0;
  std::vector<std::vector<uint8_t>> sent;
  void send_i2c_message(const uint8_t *p, uint8_t n) { sent.emplace_back(p,p+n); }
  void ack_tx_() { ++tx_acks; }
  void parse_status_message(const uint8_t*) { ++parsed; }
  void parse_notification_message(const uint8_t*) {}
  void check_clock_drift_(bool,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t) {}
  uint8_t hex_to_byte(char,char);
  uint8_t calc_checksum(const uint8_t*,uint8_t);
  void process_proxy_message(const char*);
  void process_i2c_message(const uint8_t*,uint8_t);
  bool is_plausible_status(const uint8_t*,uint16_t);
  void send_temperature_command(float);
};
struct SerialStub {
  template<class T> void print(T) {}
  template<class T> void println(T) {}
} Serial;
unsigned proxy_sends=0;
uint8_t proxy_byte=0;
void sendToI2C(uint8_t *p, uint8_t n) { ++proxy_sends; if(n) proxy_byte=p[0]; }
'''

MAIN = r'''
int main(int argc, char **argv) {
  assert(argc==2);
  std::string c=argv[1];
  GeckoSpa g;
  if(c=="proxy_short_hex") {
    char *line=strdup("RX:18:00");
    g.process_proxy_message(line);
    free(line);
  } else if(c=="proxy_oversize") {
    std::string line="RX:200:" + std::string(400,'0');
    g.process_proxy_message(line.c_str());
  } else if(c=="invalid_hex_becomes_zero") {
    processUartCommand("TX:GG");
    assert(proxy_sends==1 && proxy_byte==0);
  } else if(c=="zero_length_connected") {
    g.process_proxy_message("RX:0:");
    assert(g.connected_);
  } else if(c=="arbitrary_config_ack") {
    uint8_t p[33]{};
    g.process_i2c_message(p,sizeof(p));
    assert(g.sent.size()==1 && g.sent[0].size()==15);
  } else if(c=="corrupt_program_accepted") {
    uint8_t p[18]{};
    p[16]=4; p[17]=255;
    g.process_i2c_message(p,sizeof(p));
    assert(g.program_id_==4);
  } else if(c=="stale_fragment_combined") {
    uint8_t first[78]{}; first[1]=9; first[9]=1;
    g.process_i2c_message(first,sizeof(first));
    fake_now+=3600000;
    uint8_t last[78]{}; last[1]=9;
    g.process_i2c_message(last,sizeof(last));
    assert(g.parsed==1 && g.status_msg_len_==124);
  } else if(c=="overflow_keeps_prefix") {
    uint8_t part[78]{}; part[1]=9; part[9]=1;
    for(int i=0;i<9;++i) g.process_i2c_message(part,sizeof(part));
    assert(g.msg_buffer_len_==496);
  } else if(c=="nan_temperature_sent") {
    g.send_temperature_command(NAN);
    assert(g.sent.size()==1 && g.sent[0].size()==21);
  } else if(c=="temperature_golden") {
    g.config_version_=82; g.status_version_=81;
    g.send_temperature_command(37.0f);
    assert(g.sent.size()==1 && g.sent[0][18]==2 && g.sent[0][19]==154);
    uint8_t x=0; for(auto b:g.sent[0]) x^=b; assert(x==0);
  } else if(c=="temperature_range") {
    g.send_temperature_command(25.9f); g.send_temperature_command(40.1f);
    assert(g.sent.empty());
  } else { return 2; }
  puts("probe assertions satisfied");
}
'''


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    cpp = (ROOT / "components/gecko_spa/gecko_spa.cpp").read_text()
    avr = (ROOT / "arduino/src/main.cpp").read_text()
    signatures = [
        "uint8_t GeckoSpa::hex_to_byte(", "uint8_t GeckoSpa::calc_checksum(",
        "void GeckoSpa::process_proxy_message(", "void GeckoSpa::process_i2c_message(",
        "bool GeckoSpa::is_plausible_status(", "void GeckoSpa::send_temperature_command(",
    ]
    snippets = [function(cpp, s) for s in signatures]
    snippets += [function(avr, s) for s in (
        "uint8_t hexCharToNibble(", "uint8_t hexToByte(", "void processUartCommand(")]
    source = BUILD / "original_probes.cpp"
    source.write_text(PRELUDE + "\n".join(snippets) + MAIN)
    binary = BUILD / "original_probes"
    subprocess.run(["g++", "-std=c++17", "-g", "-O1", "-fno-omit-frame-pointer",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                    str(source), "-o", str(binary)], check=True)
    cases = {
        "proxy_short_hex": "AddressSanitizer: heap-buffer-overflow",
        "proxy_oversize": "AddressSanitizer: stack-buffer-overflow",
        "invalid_hex_becomes_zero": None,
        "zero_length_connected": None,
        "arbitrary_config_ack": None,
        "corrupt_program_accepted": None,
        "stale_fragment_combined": None,
        "overflow_keeps_prefix": None,
        "nan_temperature_sent": None,
        "temperature_golden": None,
        "temperature_range": None,
    }
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1")
    failures = 0
    report = []
    for name, sanitizer in cases.items():
        result = subprocess.run([str(binary), name], text=True, capture_output=True, env=env)
        (BUILD / f"{name}.log").write_text(result.stdout + result.stderr)
        ok = (result.returncode != 0 and sanitizer in result.stderr) if sanitizer else result.returncode == 0
        line = f"{'REPRODUCED' if ok else 'UNEXPECTED'} {name}" if sanitizer or name not in (
            "temperature_golden", "temperature_range") else f"{'PASS' if ok else 'FAIL'} {name}"
        report.append(line)
        print(line)
        failures += not ok
    report.append(f"{len(cases)-failures}/{len(cases)} expected review observations reproduced")
    report.append("This is isolated source characterization, not a firmware build or hardware validation.")
    (BUILD / "results.txt").write_text("\n".join(report) + "\n")
    print(report[-2])
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
