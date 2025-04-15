#pragma once

#include <string_view>

namespace saucer::scripts
{
    static constexpr std::string_view webview_script = R"js(
    window.saucer = 
    {{
        windowEdge:
        {{
            top:    1 << 0,
            bottom: 1 << 1,
            left:   1 << 2,
            right:  1 << 3,
        }},
        internal: 
        {{
            idc: 0,
            rpc: [],
            send: async (message, serializer = JSON.stringify) =>
            {{
                const id = ++window.saucer.internal.idc;

                const promise = new Promise((resolve, reject) => {{
                    window.saucer.internal.rpc[id] = {{
                        reject,
                        resolve,
                    }};
                }});

                await window.saucer.internal.message(serializer({{
                    ...message,
                    id
                }}));

                return promise;
            }},
            fire: async (id, message) =>
            {{
                await window.saucer.internal.message(JSON.stringify({{
                    [id]: true,
                    ...message,
                }}));
            }},
            {internal}
        }},
        {stubs}
    }};

    document.addEventListener("mousedown", async ({{ x, y, target, button, detail }}) => 
    {{
        if (button !== 0)
        {{
            return;
        }}

        const elements  = document.elementsFromPoint(x, y);
        
        const has       = (name) => elements.some(elem => elem.hasAttribute(name));
        const attribute = (name) => elements.find(elem => elem.hasAttribute(name))?.getAttribute(name);

        if (has("data-webview-close"))
        {{
            await window.saucer.close();
            return;
        }}

        if (has("data-webview-minimize"))
        {{
            await window.saucer.minimize(true);
            return;
        }}

        const maximize = attribute("data-webview-maximize");

        if (maximize === "" || (maximize === "double" && detail === 2))
        {{
            const maximized = await window.saucer.maximized();
            await window.saucer.maximize(!maximized);
            return;
        }}

        if (has("data-webview-ignore"))
        {{
            return;
        }}

        if (has("data-webview-drag"))
        {{
            await window.saucer.startDrag();
            return;
        }}

        const resize = attribute("data-webview-resize");

        if (resize == null)
        {{
            return;
        }}

        const edges      = Object.keys(window.saucer.windowEdge);
        const edge       = [...resize].reduce((flag, value) => flag | window.saucer.windowEdge[edges.find(x => x.startsWith(value))], 0);

        await window.saucer.startResize(edge);
    }});
    )js";

    static constexpr std::string_view smartview_script = R"js(
    window.saucer.internal.resolve = async (id, value) =>
    {{
        await window.saucer.internal.message({serializer}({{
                ["saucer:resolve"]: true,
                id,
                result: value === undefined ? null : value,
        }}));
    }}
    
    window.saucer.call = async (name, params) =>
    {{
        if (!Array.isArray(params))
        {{
            throw 'Bad arguments, expected array';
        }}

        if (typeof name !== 'string' && !(name instanceof String))
        {{
            throw 'Bad name, expected string';
        }}

        return window.saucer.internal.send({{
            ["saucer:call"]: true,
            name,
            params,
        }}, {serializer});
    }}

    window.saucer.exposed = new Proxy({{}}, {{
        get: (_, prop) => (...args) => window.saucer.call(prop, args),
    }});
    )js";
} // namespace saucer::scripts
