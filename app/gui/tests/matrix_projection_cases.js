function check(value, message) { if (!value) throw new Error(message) }
function ids(items) {
    var result = []
    for (var i = 0; i < items.length; ++i) result.push(items[i].id)
    return result.join("|")
}
var runtime = [
    { endpointId: "MAIN RENDER", direction: "render", backend: "wasapi", channelCount: 2 },
    { endpointId: "RAW MIC IN", direction: "capture", backend: "wasapi", channelCount: 2 },
    { endpointId: "COMPOSITE IN", direction: "capture", backend: "wasapi", channelCount: 2 },
    { endpointId: "BGM RENDER", direction: "render", backend: "wasapi", channelCount: 2 },
    { endpointId: "MOTU ASIO", direction: "capture", backend: "physical-asio", channelCount: 2 }
]
var inputs = [
    { id: "RAW MIC IN.ch1", label: "RAW MIC IN Ch 1" },
    { id: "COMPOSITE IN.ch1", label: "COMPOSITE IN Ch 1" },
    { id: "BGM.ch1", label: "BGM Ch 1" },
    { id: "MIC.ch1", label: "MIC Ch 1" },
    { id: "RAW MIC IN.ch3", label: "Out of range" },
    { id: "MAIN RENDER.ch1", label: "Wrong direction" },
    { id: "asio-output-1", label: "ASIO DAW Out 1" },
    { id: "virtual-wasapi-lab-1.ch1", label: "Virtual WASAPI Lab 1" },
    { id: "custom-source", label: "Custom source" },
    { id: "MOTU ASIO.ch1", label: "Physical ASIO Ch 1" }
]
var current = projectEndpoints(inputs, true, "matrix", false, true, runtime)
check(ids(current) === "RAW MIC IN.ch1|COMPOSITE IN.ch1|asio-output-1|" +
      "virtual-wasapi-lab-1.ch1|custom-source|MOTU ASIO.ch1",
      "only configured, ASIO, lab and unclassified inputs should be shown")
check(current[0].family === "wasapi" && current[1].family === "wasapi",
      "WASAPI device families should come from the actual binding")
check(current[5].active, "physical ASIO input should remain active")
var all = projectEndpoints(inputs, true, "matrix", true, true, runtime)
check(all.length === inputs.length && !all[2].active && !all[3].active &&
      !all[4].active && !all[5].active, "unbound channels should be visible but inactive")
check(inputs.length === 10 && inputs[2].id === "BGM.ch1" && inputs[2].active === undefined,
      "projection must not modify the saved graph")
var outputs = [
    { id: "MAIN RENDER.ch1", label: "MAIN RENDER Ch 1" },
    { id: "AUX OUT.ch1", label: "AUX OUT Ch 1" },
    { id: "asio-input-1", label: "ASIO DAW In 1" }
]
check(ids(projectEndpoints(outputs, false, "matrix", false, true, runtime)) ===
      "MAIN RENDER.ch1|asio-input-1", "unbound output should be hidden")
check(projectEndpoints(inputs, true, "matrix", false, false, []).length === inputs.length,
      "unknown runtime configuration must not hide endpoints")
check(projectEndpoints(inputs, true, "duplex", false, true, runtime).length === inputs.length,
      "legacy duplex projection must not change")
var changedRuntime = [
    { endpointId: "BGM", direction: "capture", backend: "wasapi", channelCount: 1 }
]
var switched = projectEndpoints(inputs, true, "matrix", false, true, changedRuntime)
check(ids(switched).indexOf("BGM.ch1") >= 0 &&
      ids(switched).indexOf("RAW MIC IN.ch1") < 0,
      "changing the runtime should change the visible bound channels")
check(inputs.length === 10 && inputs[0].id === "RAW MIC IN.ch1",
      "switching runtimes must not delete the original channel")
var renderInputs = [
    { id: "wasapi-capture-l", label: "WASAPI Capture L" },
    { id: "asio-output-l", label: "ASIO DAW Out L" }
]
check(ids(projectEndpoints(renderInputs, true, "render", false, true, [])) ===
      "asio-output-l", "legacy render mode should hide the capture input")
