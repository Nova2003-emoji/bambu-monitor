import { ESP_ROM_BAUD } from "./const.js";
import { ESPLoader } from "./esp_loader.js";
export { ESPLoader } from "./esp_loader.js";
export { CHIP_FAMILY_ESP32, CHIP_FAMILY_ESP32S2, CHIP_FAMILY_ESP8266, } from "./const.js";
export const connect = async (logger) => {
    // - Request a port and open a connection.
    const port = await navigator.serial.requestPort();
    logger.log("Connecting...");
    await port.open({ baudRate: ESP_ROM_BAUD });
    logger.log("Connected successfully.");
    return new ESPLoader(port, logger);
};
