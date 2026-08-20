// here, common errors are defined

#ifndef ERRORS_H
#define ERRORS_H

// business
#define ERR_BUSINESS_STATES 0x100
#define ERR_BUSINESS_PPG 0x200

// communication
#define ERR_COMMUNICATION_SENDING 0x300
#define ERR_COMMUNICATION_RECEIVING 0x400
// pairing, bonding and link encryption - its own base rather than a sending
// or receiving code, because a security failure is neither.
#define ERR_COMMUNICATION_SECURITY 0x500

// hal
#define ERR_HAL_POWER 0x600
#define ERR_HAL_SKIN_TEMPERATURE 0x700
#define ERR_HAL_MOTION 0x800
#define ERR_HAL_LEDS 0x900
#define ERR_HAL_DEVICE_IDENTITY 0xA00

#endif /* ERRORS_H */
