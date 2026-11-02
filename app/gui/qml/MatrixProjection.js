.pragma library

function endpointFamily(endpoint, index, count) {
    if (endpoint.family !== undefined)
        return endpoint.family
    var identity = (String(endpoint.id) + " " + String(endpoint.label)).toLowerCase()
    if (identity.indexOf("asio") >= 0 || identity.indexOf("daw") >= 0 ||
            identity.indexOf("virtual") >= 0)
        return "asio"
    if (identity.indexOf("wasapi") >= 0 || identity.indexOf("capture") >= 0 ||
            identity.indexOf("render") >= 0 || identity.indexOf("monitor") >= 0 ||
            identity.indexOf("hardware") >= 0)
        return "wasapi"
    if (count === 4)
        return index < 2 ? "wasapi" : "asio"
    return "other"
}

function runtimeBinding(endpointId, source, runtimeEndpoints) {
    var expectedDirection = source ? "capture" : "render"
    for (var index = 0; index < runtimeEndpoints.length; ++index) {
        var runtime = runtimeEndpoints[index]
        if (runtime.direction !== expectedDirection)
            continue
        var prefix = String(runtime.endpointId) + ".ch"
        if (endpointId.indexOf(prefix) !== 0)
            continue
        var suffix = endpointId.substring(prefix.length)
        if (!/^[1-9][0-9]*$/.test(suffix))
            continue
        if (Number(suffix) <= Number(runtime.channelCount))
            return runtime
    }
    return null
}

function projectEndpoints(endpoints, source, runtimeMode, includeInactive,
                          runtimeConfigured, runtimeEndpoints) {
    var projected = []
    for (var index = 0; index < endpoints.length; ++index) {
        var endpoint = endpoints[index]
        var endpointId = String(endpoint.id)
        var family = endpointFamily(endpoint, index, endpoints.length)
        var active = runtimeMode === "duplex" || runtimeMode === "matrix" ||
                (source ? family !== "wasapi" : family !== "asio")

        if (runtimeMode === "matrix" && runtimeConfigured) {
            var binding = runtimeBinding(endpointId, source, runtimeEndpoints)
            if (binding !== null && binding.backend === "wasapi")
                family = "wasapi"
            var runtimeChannel = /\.ch[1-9][0-9]*$/.test(endpointId)
            var labSource = source &&
                    /^virtual-wasapi-lab-[1-9][0-9]*\.ch[1-9][0-9]*$/.test(endpointId)
            if (runtimeChannel && binding === null && !labSource)
                active = false
        }
        if (active || includeInactive) {
            projected.push({ id: endpoint.id, label: endpoint.label,
                             active: active, family: family })
        }
    }
    return projected
}
