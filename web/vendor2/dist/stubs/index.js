import { CHIP_FAMILY_ESP32, CHIP_FAMILY_ESP32S2, CHIP_FAMILY_ESP8266, } from "../const.js";
import { toByteArray } from "../util.js";
export const getStubCode = async (chipFamily) => {
    let stubcode;
    if (chipFamily == CHIP_FAMILY_ESP32) {
        stubcode = await (await fetch(new URL("./esp32.json", import.meta.url))).json();
    }
    else if (chipFamily == CHIP_FAMILY_ESP32S2) {
        stubcode = await (await fetch(new URL("./esp32s2.json", import.meta.url))).json();
    }
    else if (chipFamily == CHIP_FAMILY_ESP8266) {
        stubcode = await (await fetch(new URL("./esp8266.json", import.meta.url))).json();
    }
    // Base64 decode the text and data
    return {
        ...stubcode,
        text: toByteArray(atob(stubcode.text)),
        data: toByteArray(atob(stubcode.data)),
    };
};
