
var LibraryEmulatorJS = {
    $EmulatorJSGetState: function() {
        let info = _save_state_info();
        let state = UTF8ToString(info).split("|");
        if (state[2] !== "1") {
            console.error("Failed to save state!", state[0]);
            throw new Error(state[0]);
        }
        const size = parseInt(state[0]);
        const dataStart = parseInt(state[1]);
        // Copy the state out of the heap before freeing the buffer it lives in
        const data = new Uint8Array(HEAPU8.subarray(dataStart, dataStart + size));
        _free(dataStart);
        return data;
    },
    $EmulatorJSGetMemoryData: function(key) {
        let keyPtr = stringToNewUTF8(key);
        const info = _get_memory_data(keyPtr);
        _free(keyPtr);
        if (!info) return;
        const data_info = UTF8ToString(info).split("|");
        _free(info);
        const size = parseInt(data_info[0]);
        const dataStart = parseInt(data_info[1]);
        const data = HEAPU8.subarray(dataStart, dataStart + size);
        return data;
    },

    EmulatorJSShowKeyboard__deps: ['$Asyncify', 'malloc'],
    EmulatorJSShowKeyboard__async: true,
    EmulatorJSShowKeyboard__sig: 'iiii',
    EmulatorJSShowKeyboard: function(hintPtr, maxLength, passwordMode) {
        const hint = (hintPtr && UTF8ToString(hintPtr)) || "";
        return Asyncify.handleSleep(function(wakeUp) {
            const respond = function(result) {
                if (typeof result !== "string") {
                    wakeUp(0);
                    return;
                }
                const lenBytes = lengthBytesUTF8(result) + 1;
                const ptr = _malloc(lenBytes);
                stringToUTF8(result, ptr, lenBytes);
                wakeUp(ptr);
            };
            const fn = Module.getInputText;
            if (typeof fn !== "function") {
                console.warn("EmulatorJSShowKeyboard: Module.getInputText not set, falling back to prompt()");
                respond(window.prompt(hint || "Enter text:", "") || null);
                return;
            }
            Promise.resolve(fn({ hint: hint, maxLength: maxLength, password: !!passwordMode }))
                .then(respond)
                .catch(function(err) {
                    console.error("getInputText threw:", err);
                    respond(null);
                });
        });
    },
};

addToLibrary(LibraryEmulatorJS);
