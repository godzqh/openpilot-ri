// CAN msgs we care about
#define MAZDA_LKAS          0x243
#define MAZDA_LKAS2         0x249
#define MAZDA_LKAS_HUD      0x440
#define MAZDA_CRZ_CTRL      0x21c
#define MAZDA_CRZ_BTNS      0x09d
#define TI_STEER_TORQUE     0x24A
#define MAZDA_STEER_TORQUE  0x240
#define MAZDA_ENGINE_DATA   0x202
#define MAZDA_PEDALS        0x165

// CAN bus numbers
#define MAZDA_MAIN 0
#define MAZDA_AUX  1
#define MAZDA_CAM  2

const SteeringLimits MAZDA_STEERING_LIMITS = {
  .max_steer = 800,
  .max_rate_up = 10,
  .max_rate_down = 25,
  .max_rt_delta = 300,
  .max_rt_interval = 250000,
  .driver_torque_factor = 1,
  .driver_torque_allowance = 15,
  .type = TorqueDriverLimited,
};

const CanMsg MAZDA_TX_MSGS[] = {{MAZDA_LKAS, 0, 8},{MAZDA_LKAS2, 1, 8}, {MAZDA_CRZ_BTNS, 0, 8}, {MAZDA_LKAS_HUD, 0, 8}};

RxCheck mazda_rx_checks[] = {
  {.msg = {{MAZDA_CRZ_CTRL,     0, 8, .frequency = 50U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_CRZ_BTNS,     0, 8, .frequency = 10U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_STEER_TORQUE, 0, 8, .frequency = 83U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_ENGINE_DATA,  0, 8, .frequency = 100U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_PEDALS,       0, 8, .frequency = 50U}, { 0 }, { 0 }}},
};

RxCheck mazda_ti_rx_checks[] = {
  {.msg = {{MAZDA_CRZ_CTRL,     0, 8, .frequency = 50U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_CRZ_BTNS,     0, 8, .frequency = 10U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_STEER_TORQUE, 0, 8, .frequency = 83U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_ENGINE_DATA,  0, 8, .frequency = 100U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_PEDALS,       0, 8, .frequency = 50U}, { 0 }, { 0 }}},
  {.msg = {{TI_STEER_TORQUE,    1, 8, .frequency = 50U}, { 0 }, { 0 }}},
};

/*
bool valid = addr_safety_check(to_push, &mazda_rx_checks, NULL, NULL, NULL, NULL);
  if (((GET_ADDR(to_push) == TI_STEER_TORQUE)) &&
      ((GET_BYTE(to_push, 0) == GET_BYTE(to_push, 1)))) {
    torque_interceptor_detected = 1;
    valid &= addr_safety_check(to_push, &mazda_ti_rx_checks, NULL, NULL, NULL, NULL);
  }
  
  if (valid && ((int)GET_BUS(to_push) == MAZDA_MAIN)) {
*/
bool enable_ti = false;
const uint16_t MAZDA_PARAM_ENABLE_TI = 1;

// track msgs coming from OP so that we know what CAM msgs to drop and what to forward
static void mazda_rx_hook(CANPacket_t *to_push) {
  if ((int)GET_BUS(to_push) == MAZDA_MAIN) {
    int addr = GET_ADDR(to_push);

    if (addr == MAZDA_ENGINE_DATA) {
      // sample speed: scale by 0.01 to get kph
      int speed = (GET_BYTE(to_push, 2) << 8) | GET_BYTE(to_push, 3);
      vehicle_moving = speed > 10; // moving when speed > 0.1 kph
    }

    if (addr == MAZDA_STEER_TORQUE && !torque_interceptor_detected) {
      int torque_driver_new = GET_BYTE(to_push, 0) - 127U;
      // update array of samples
      update_sample(&torque_driver, torque_driver_new);
    }

    // enter controls on rising edge of ACC, exit controls on ACC off
    if (addr == MAZDA_CRZ_CTRL) {
      acc_main_on = GET_BIT(to_push, 17U);
      bool cruise_engaged = GET_BYTE(to_push, 0) & 0x8U;
      pcm_cruise_check(cruise_engaged);
    }

    if (addr == MAZDA_ENGINE_DATA) {
      gas_pressed = (GET_BYTE(to_push, 4) || (GET_BYTE(to_push, 5) & 0xF0U));
    }

    if (addr == MAZDA_PEDALS) {
      brake_pressed = (GET_BYTE(to_push, 0) & 0x10U);
    }

    generic_rx_checks((addr == MAZDA_LKAS));
  }

  if ((GET_BUS(to_push) == MAZDA_AUX)) {
    int addr = GET_ADDR(to_push);
    if (addr == TI_STEER_TORQUE) {
      int torque_driver_new = GET_BYTE(to_push, 0) - 126;
      update_sample(&torque_driver, torque_driver_new);
      torque_interceptor_detected = 1;
    }
  }
}

static bool mazda_tx_hook(CANPacket_t *to_send) {
  bool tx = true;
  int addr = GET_ADDR(to_send);
  int bus = GET_BUS(to_send);

  // Check if msg is sent on the main BUS
  if (bus == MAZDA_MAIN) {

    // steer cmd checks
    if (addr == MAZDA_LKAS) {
      int desired_torque = (((GET_BYTE(to_send, 0) & 0x0FU) << 8) | GET_BYTE(to_send, 1)) - 2048U;

      if (steer_torque_cmd_checks(desired_torque, -1, MAZDA_STEERING_LIMITS)) {
        tx = false;
      }
    }

    // cruise buttons check
    if (addr == MAZDA_CRZ_BTNS) {
      // allow resume spamming while controls allowed, but
      // only allow cancel while contrls not allowed
      bool cancel_cmd = (GET_BYTE(to_send, 0) == 0x1U);
      if (!controls_allowed && !cancel_cmd) {
        tx = false;
      }
    }
  }

  return tx;
}

static int mazda_fwd_hook(int bus, int addr) {
  int bus_fwd = -1;
  bool block = (addr == MAZDA_LKAS2);
  if (bus == MAZDA_MAIN) {
    if (!block) {
      bus_fwd = MAZDA_CAM;
    }
  } else if (bus == MAZDA_CAM) {
    block |= (addr == MAZDA_LKAS) || (addr == MAZDA_LKAS_HUD);
    if (!block) {
      bus_fwd = MAZDA_MAIN;
    }
  } else {
    // don't fwd
  }

  return bus_fwd;
}

static safety_config mazda_init(uint16_t param) {
  safety_config ret;
  enable_ti = GET_FLAG(param, MAZDA_PARAM_ENABLE_TI);
  if (enable_ti) {
    ret = BUILD_SAFETY_CFG(mazda_ti_rx_checks, MAZDA_TX_MSGS);
  } else {
    ret = BUILD_SAFETY_CFG(mazda_rx_checks, MAZDA_TX_MSGS);
  }
  return ret;
}

const safety_hooks mazda_hooks = {
  .init = mazda_init,
  .rx = mazda_rx_hook,
  .tx = mazda_tx_hook,
  .fwd = mazda_fwd_hook,
};

/*
  Mazda Gen 4 2019+
  Mazda 3 2019+
  CX-30,50,90
*/ 
#define MAZDA_2019_BRAKE          0x43F  // main bus
#define MAZDA_2019_GAS            0x202  // camera bus DBC: ENGINE_DATA
#define MAZDA_2019_CRUISE         0x44A  // main bus. DBC: CRUISE_STATE
#define MAZDA_2019_SPEED          0x217  // camera bus. DBC: SPEED
#define MAZDA_2019_STEER_TORQUE   0x24B  // aux bus. DBC: EPS_FEEDBACK
#define MAZDA_2019_LKAS           0x249  // aux bus. DBC: EPS_LKAS
#define MAZDA_2019_CRZ_BTNS       0x9d   // rx on main tx on camera. DBC: CRZ_BTNS
#define MAZDA_2019_ACC            0x220  // main bus. DBC: ACC

const SteeringLimits MAZDA_2019_STEERING_LIMITS = {
  .max_steer = 8000,
  .max_rate_up = 45,
  .max_rate_down = 80,
  .max_rt_delta = 1688,  // (45*100hz*250000/1000000)*1.5
  .max_rt_interval = 250000,
  .driver_torque_factor = 1,
  .driver_torque_allowance = 1400,
  .type = TorqueDriverLimited,
};

const CanMsg MAZDA_2019_TX_MSGS[] = {{MAZDA_2019_LKAS, 1, 8}, {MAZDA_2019_ACC, 2, 8}};

// start with high expected_timestep
RxCheck mazda_2019_rx_checks[] = {
  {.msg = {{MAZDA_2019_BRAKE,         0, 8, .frequency = 20U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_2019_GAS,           2, 8, .frequency = 100U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_2019_CRUISE,        0, 8, .frequency = 10U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_2019_SPEED,         2, 8, .frequency = 30U}, { 0 }, { 0 }}},
  {.msg = {{MAZDA_2019_STEER_TORQUE,  1, 8, .frequency = 50U}, { 0 }, { 0 }}},
};

static void mazda_2019_rx_hook(CANPacket_t *to_push) {
  static bool cruise_engaged;
  static int speed;
  int bus = GET_BUS(to_push);
  int addr = GET_ADDR(to_push);

  switch (bus) {
    case MAZDA_MAIN:
      switch (addr) {
        case MAZDA_2019_BRAKE:
          brake_pressed = (GET_BYTE(to_push, 5) & 0x4U);
          break;  // end MAZDA_2019_BRAKE

        case MAZDA_2019_CRUISE:
          acc_main_on = true;
          cruise_engaged = GET_BYTE(to_push, 0) & 0x20U;
          bool pre_enable = GET_BYTE(to_push, 0) & 0x40U;
          pcm_cruise_check((cruise_engaged || pre_enable));
          break;  // end MAZDA_2019_CRUISE

        default:  // default address main
          break;
      }
      break;  // end MAZDA_MAIN

    case MAZDA_CAM:
      switch (addr) {
        case MAZDA_2019_GAS:
          gas_pressed = (GET_BYTE(to_push, 4) || (GET_BYTE(to_push, 5) & 0xC0U));
          break;  // end MAZDA_2019_GAS

        case MAZDA_2019_SPEED:
          // sample speed: scale by 0.01 to get kph
          speed = (GET_BYTE(to_push, 4) << 8) | (GET_BYTE(to_push, 5));
          vehicle_moving = (speed > 10);  // moving when speed > 0.1 kph
          break;  // end MAZDA_2019_SPEED

        default:  // default address cam
          break;
      }
      break;  // end MAZDA_CAM

    case MAZDA_AUX:
      switch (addr) {
        case MAZDA_2019_STEER_TORQUE:
          update_sample(&torque_driver, (int16_t)(GET_BYTE(to_push, 0) << 8 | GET_BYTE(to_push, 1)));
          break;  // end TI2_STEER_TORQUE

        default:  // default address aux
          break;
      }
      break;  // end MAZDA_AUX

    default:  // default bus
      break;
  }
  generic_rx_checks((addr == MAZDA_2019_SPEED) && (bus == MAZDA_MAIN));

}

static bool mazda_2019_tx_hook(CANPacket_t *to_send) {
  int tx = true;
  int addr = GET_ADDR(to_send);
  int bus = GET_BUS(to_send);

  if (bus == MAZDA_AUX) {
    if (addr == MAZDA_2019_LKAS) {
      int desired_torque = (int16_t)((GET_BYTE(to_send, 0) << 8) | GET_BYTE(to_send, 1));  // signal is signed
      if (steer_torque_cmd_checks(desired_torque, -1, MAZDA_2019_STEERING_LIMITS)) {
        tx = false;
      }
    }
  }
  return tx;
}

static int mazda_2019_fwd_hook(int bus, int addr) {
  int bus_fwd = -1;
  bool block = false;

  if (bus == MAZDA_MAIN) {
    block = (addr == MAZDA_2019_ACC);
    if (!block) {
      bus_fwd = MAZDA_CAM;
    }

  } else if (bus == MAZDA_CAM) {
    if (!block) {
      bus_fwd = MAZDA_MAIN;
    }
  } else {
    // don't fwd
  }

  return bus_fwd;
}

static safety_config mazda_2019_init(uint16_t param) {
  UNUSED(param);
  return BUILD_SAFETY_CFG(mazda_2019_rx_checks, MAZDA_2019_TX_MSGS);
}

const safety_hooks mazda_2019_hooks = {
  .init = mazda_2019_init,
  .rx = mazda_2019_rx_hook,
  .tx = mazda_2019_tx_hook,
  .fwd = mazda_2019_fwd_hook,
};