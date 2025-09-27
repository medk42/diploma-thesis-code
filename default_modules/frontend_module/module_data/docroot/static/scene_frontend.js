// static/my_file.js
(function () {
    function load(url) {
        return new Promise((res, rej) => {
            const s = document.createElement('script');
            s.src = url; s.onload = res; s.onerror = rej; document.head.appendChild(s);
        });
    }

    function waitForEl(id, timeoutMs = 5000) {
        return new Promise((resolve, reject) => {
            const el = document.getElementById(id);
            if (el) return resolve(el);

            const obs = new MutationObserver(() => {
                const e = document.getElementById(id);
                if (e) { obs.disconnect(); resolve(e); }
            });
            obs.observe(document.documentElement, { childList: true, subtree: true });

            if (timeoutMs) setTimeout(() => { obs.disconnect(); reject(new Error('timeout')); }, timeoutMs);
        });
    }

    (async () => {
        try {
            // load required scripts
            await load("static/three.min.js");
            await load("static/OrbitControls.js");

            // Get the host element
            const host = await waitForEl("scene-container"); // <-- waits until Wt inserts it


            // Setup scene + renderer
            const scene = new THREE.Scene();
            scene.background = new THREE.Color(0xf4f4f4);

            const renderer = new THREE.WebGLRenderer({ antialias: true });
            // renderer.setPixelRatio(window.devicePixelRatio || 1);
            host.appendChild(renderer.domElement);  


            // Setup camera
            const camera = new THREE.PerspectiveCamera(60, host.clientWidth / host.clientHeight, 0.1, 1000);
            camera.position.set(5, 5, 7);
            camera.lookAt(0, 0, 0);

            
            // Handle resizing
            function sizeToHost() {
                const w = host.clientWidth, h = host.clientHeight;
                console.log("Resizing to", w, h);

                if (!w || !h) return; // skip while hidden
                renderer.setSize(w, h, false);
                camera.aspect = w / h;
                camera.updateProjectionMatrix();
            }
            new ResizeObserver(() => { console.log("ResizeObserver"); sizeToHost(); }).observe(host);
            sizeToHost();


            // Setup orbit controls
            const controls = new THREE.OrbitControls(camera, renderer.domElement);
            controls.enableDamping = true;


            // Setup lighting
            scene.add(new THREE.HemisphereLight(0xffffff, 0x555555, 0.6));
            const dir = new THREE.DirectionalLight(0xffffff, 0.9);
            dir.position.set(5, 8, 6); scene.add(dir);


            // Add some demo objects
            scene.add(new THREE.GridHelper(20, 20));
            const geo = new THREE.BoxGeometry(1, 1, 1);
            for (let i = 0; i < 10; i++) {
                const mat = new THREE.MeshStandardMaterial();
                mat.color.setHSL(Math.random(), 0.6, 0.55);
                const m = new THREE.Mesh(geo, mat);
                m.position.set((Math.random() - 0.5) * 10, Math.random() * 4 + 0.5, (Math.random() - 0.5) * 10);
                scene.add(m);
            }
            

            // Handle animating
            (function animate() { requestAnimationFrame(animate); controls.update(); renderer.render(scene, camera); })();


            window.startCounterSocket = function(url) {
                const ws = new WebSocket(url);
                ws.binaryType = "arraybuffer";

                ws.onopen = () => console.log("CounterWS open:", url);
                ws.onmessage = ev => {
                    return; // disable for now
                    console.log("is ArrayBuffer:", ev.data instanceof ArrayBuffer);
                    const buf = ev.data instanceof ArrayBuffer
                    ? ev.data
                    : (ev.data.arrayBuffer ? ev.data.arrayBuffer() : null);

                    if (buf) {
                        const text = new TextDecoder().decode(new Uint8Array(buf));
                        console.log("CounterWS:", text);
                    }
                    else {
                        console.log("CounterWS, buf:", buf);
                    }
                };
                ws.onclose = () => console.log("CounterWS closed");
            };
            if (window.CounterWS_URL) window.startCounterSocket(window.CounterWS_URL);
        } catch (e) {
            console.error("three.js bootstrap failed:", e);
        }
    })();
})();
