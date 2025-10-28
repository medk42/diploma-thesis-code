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

    // Reader for little-endian binary data
    class Reader {
        constructor(buf) {
            this.dv = new DataView(buf);
            this.o = 0;
            this.len = this.dv.byteLength;
        }
        remaining() { return this.len - this.o; }
        ensure(n) {
            if (this.o + n > this.len) throw new Error(`Buffer underflow: need ${n}, have ${this.remaining()} at ${this.o}`);
        }
        u8() { this.ensure(1); const v = this.dv.getUint8(this.o); this.o += 1; return v; }
        u32() { this.ensure(4); const v = this.dv.getUint32(this.o, true); this.o += 4; return v; }
        f32() { this.ensure(4); const v = this.dv.getFloat32(this.o, true); this.o += 4; return v; }
        pose7() {
            const tx = this.f32(), ty = this.f32(), tz = this.f32();
            const qx = this.f32(), qy = this.f32(), qz = this.f32(), qw = this.f32();
            return { t: { x: tx, y: ty, z: tz }, q: { x: qx, y: qy, z: qz, w: qw } };
        }
    }

    // Axis conversion toggle: if your incoming world is Z-up, set INCOMING_Z_UP = true
    const INCOMING_Z_UP = true;
    let U2T = null;
    let U2T_INV = null;
    function toThreePosition(v) {
        const vec = new THREE.Vector3(v.x, v.y, v.z);
        let orig = vec.clone();
        if (INCOMING_Z_UP) vec.applyQuaternion(U2T);  // (x, z, -y)
        // console.log(`Pos: incoming ${orig.x.toFixed(2)},${orig.y.toFixed(2)},${orig.z.toFixed(2)} -> three.js ${vec.x.toFixed(2)},${vec.y.toFixed(2)},${vec.z.toFixed(2)}`);
        return vec;
    }
    function toThreeQuaternion(q) {
        let qU = new THREE.Quaternion(q.x, q.y, q.z, q.w);
        let orig = qU.clone();
        if (INCOMING_Z_UP) quat = U2T.clone().multiply(qU).multiply(U2T_INV).normalize(); // q_R * qU * q_R^-1
        else quat = qU;
        // console.log(`Quat: incoming ${orig.x.toFixed(2)},${orig.y.toFixed(2)},${orig.z.toFixed(2)},${orig.w.toFixed(2)} -> three.js ${quat.x.toFixed(2)},${quat.y.toFixed(2)},${quat.z.toFixed(2)},${quat.w.toFixed(2)}`);
        return quat;
    }
    function applyPose(obj, pose) {
        obj.position.copy(toThreePosition(pose.t));
        obj.quaternion.copy(toThreeQuaternion(pose.q));
    }

    // Color/material helpers
    const materialCache = new Map(); // key: rgba32 -> MeshStandardMaterial
    function colorKey(r, g, b, a) { return (r << 24) | (g << 16) | (b << 8) | (a >>> 0); }
    function getMaterialFromRGBA(r, g, b, a) {
        const key = colorKey(r, g, b, a) >>> 0;
        let m = materialCache.get(key);
        if (!m) {
            m = new THREE.MeshStandardMaterial({
                color: new THREE.Color(r / 255, g / 255, b / 255),
                opacity: (a / 255),
                transparent: a < 255,
                metalness: 0.0,
                roughness: 0.8,
            });
            materialCache.set(key, m);
        }
        return m;
    }

    // Resource construction from parts
    function buildResourceGroup(parts) {
        const group = new THREE.Group();
        for (const part of parts) {
            const { type, desc, origin, color } = part;
            let geom = null;
            if (type === 0) { // Box: sx, sy, sz
                geom = new THREE.BoxGeometry(desc.sx, desc.sz, desc.sy); // note: sy <-> sz swap for Y-up
            } else if (type === 1) { // Sphere: r
                geom = new THREE.SphereGeometry(desc.r, 24, 16);
            } else if (type === 2) { // Cylinder: rTop, rBot, h (Y-axis)
                geom = new THREE.CylinderGeometry(desc.rTop, desc.rBot, desc.h, 24, 1, false);
            } else {
                console.warn('Unknown primitive type:', type); continue;
            }
            const mat = getMaterialFromRGBA(color.r, color.g, color.b, color.a);
            const mesh = new THREE.Mesh(geom, mat);
            applyPose(mesh, origin);
            group.add(mesh);
        }
        return group;
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
            const camera = new THREE.PerspectiveCamera(60, host.clientWidth / host.clientHeight, 0.01, 10);
            camera.position.set(0.5, 0.5, 0.7);
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
            controls.enableDamping = false;


            // Setup lighting
            scene.add(new THREE.HemisphereLight(0xffffff, 0x555555, 0.6));
            const dir = new THREE.DirectionalLight(0xffffff, 0.9);
            dir.position.set(5, 8, 6); scene.add(dir);


            // Add some demo objects
            const gridHelper = new THREE.GridHelper(2, 20);
            scene.add(gridHelper);

            // Handle animating
            (function animate() { requestAnimationFrame(animate); controls.update(); renderer.render(scene, camera); })();


            U2T = new THREE.Quaternion().setFromEuler(new THREE.Euler(-Math.PI / 2, 0, 0)); // q_R
            U2T_INV = U2T.clone().invert();

            const resources = new Map(); // id -> THREE.Group (prefab)
            const objects = new Map(); // id -> THREE.Object3D (instance)
            const trajectories = new Map();// id -> { line: THREE.Line, points: Array<THREE.Vector3>, dashed: bool }
            let lastSeq = -1;


            function addOrReplaceResource(resourceId, parts) {
                const grp = buildResourceGroup(parts);
                // Replace if exists
                resources.set(resourceId, grp);
            }


            function addObject(id, resourceId, pose) {
                const prefab = resources.get(resourceId);
                if (!prefab) { console.warn(`AddObject ${id}: resource ${resourceId} not found`); return; }
                if (objects.has(id)) { console.warn(`AddObject ${id}: already exists`); return; }
                // Clone; share geometries/materials by default
                const inst = prefab.clone(true);
                applyPose(inst, pose);
                scene.add(inst);
                objects.set(id, inst);
            }


            function updateObject(id, pose) {
                const obj = objects.get(id);
                if (!obj) { console.warn(`UpdateObject ${id}: not found`); return; }
                applyPose(obj, pose);
            }


            function removeObject(id) {
                const obj = objects.get(id);
                if (!obj) { console.warn(`RemoveObject ${id}: not found`); return; }
                scene.remove(obj);
                objects.delete(id);
            }


            function addTrajectory(id, color, dashed, pts) {
                if (trajectories.has(id)) { console.warn(`AddTrajectory ${id}: already exists`); return; }
                if (pts.length < 2) { console.warn(`AddTrajectory ${id}: need at least 2 points`); return; }

                const geom = new THREE.BufferGeometry();
                const flat = new Float32Array(pts.length * 3);
                for (let i = 0; i < pts.length; ++i) { const p = pts[i]; flat[3 * i] = p.x; flat[3 * i + 1] = p.y; flat[3 * i + 2] = p.z; }
                geom.setAttribute('position', new THREE.BufferAttribute(flat, 3));
                geom.computeBoundingSphere();
                const color_three = new THREE.Color(color.r / 255, color.g / 255, color.b / 255);
                const mat = dashed
                    ? new THREE.LineDashedMaterial({ color: color_three, linewidth: 2, dashSize: 0.02, gapSize: 0.01 })
                    : new THREE.LineBasicMaterial({ color: color_three, linewidth: 2 });
                const line = new THREE.Line(geom, mat);
                if (dashed) line.computeLineDistances();
                scene.add(line);
                trajectories.set(id, { line, points: pts.slice(), dashed });
            }


            function updateTrajectory(id, newPts, removeFromHead) {
                const rec = trajectories.get(id);
                if (!rec) { console.warn(`UpdateTrajectory ${id}: not found`); return; }
                if (removeFromHead > 0) {
                    rec.points.splice(0, Math.min(removeFromHead, rec.points.length));
                }
                if (newPts.length) rec.points.push(...newPts);
                // Rebuild geometry (simple and fine for moderate sizes)
                const geom = rec.line.geometry;
                const flat = new Float32Array(rec.points.length * 3);
                for (let i = 0; i < rec.points.length; ++i) {
                    const p = rec.points[i]; flat[3 * i] = p.x; flat[3 * i + 1] = p.y; flat[3 * i + 2] = p.z;
                }
                geom.setAttribute('position', new THREE.BufferAttribute(flat, 3));
                geom.attributes.position.needsUpdate = true;
                geom.computeBoundingSphere();
                if (rec.dashed) rec.line.computeLineDistances();
            }


            function removeTrajectory(id) {
                const rec = trajectories.get(id);
                if (!rec) { console.warn(`RemoveTrajectory ${id}: not found`); return; }
                scene.remove(rec.line);
                trajectories.delete(id);
            }


            // ---------------- Decoder per SCN1 ----------------
            function decodeHeader(r) {
                const magic = r.u32();
                if (magic !== 0x314E4353) throw new Error('Bad magic: ' + magic.toString(16));
                const seq = r.u32();
                const gridCommanded = !!r.u8();
                const gridEnabled = !!r.u8();

                if (seq != lastSeq + 1) console.warn('Warning: sequence jump from', lastSeq, 'to', seq);
                lastSeq = seq;
                if (gridCommanded) gridHelper.visible = gridEnabled;
            }


            function decodeRegistrations(r) {
                const registrationCount = r.u32();
                for (let i = 0; i < registrationCount; ++i) {
                    const resourceId = r.u32();
                    const partCount = r.u32();
                    const parts = [];
                    for (let j = 0; j < partCount; ++j) {
                        const type = r.u8();
                        let desc;
                        if (type === 0) { const sx = r.f32(), sy = r.f32(), sz = r.f32(); desc = { sx, sy, sz }; }
                        else if (type === 1) { const rr = r.f32(); desc = { r: rr }; }
                        else if (type === 2) { const rt = r.f32(), rb = r.f32(), h = r.f32(); desc = { rTop: rt, rBot: rb, h }; }
                        else throw new Error('Unknown primitive type ' + type);
                        const origin = r.pose7();
                        const color = { r: r.u8(), g: r.u8(), b: r.u8(), a: r.u8() };
                        parts.push({ type, desc, origin, color });
                    }
                    addOrReplaceResource(resourceId, parts);
                }
            }

            function decodeObjects(r) {
                const objectCount = r.u32();
                for (let i = 0; i < objectCount; ++i) {
                    const id = r.u32();
                    const action = r.u8();
                    if (action === 0) { const resId = r.u32(); const pose = r.pose7(); addObject(id, resId, pose); }
                    else if (action === 1) { const pose = r.pose7(); updateObject(id, pose); }
                    else if (action === 2) { removeObject(id); }
                    else console.warn('Unknown object action', action);
                }
            }

            function decodeTrajectories(r) {
                const trajCount = r.u32();
                for (let i = 0; i < trajCount; ++i) {
                    const id = r.u32();
                    const action = r.u8();
                    if (action === 0) {
                        const color = { r: r.u8(), g: r.u8(), b: r.u8(), a: r.u8() };
                        const dashed = !!r.u8();
                        const n = r.u32();
                        const pts = new Array(n);
                        for (let k = 0; k < n; ++k) { const x = r.f32(), y = r.f32(), z = r.f32(); pts[k] = toThreePosition({ x, y, z }); }
                        addTrajectory(id, color, dashed, pts);
                    } else if (action === 1) {
                        const n = r.u32();
                        const pts = new Array(n);
                        for (let k = 0; k < n; ++k) { const x = r.f32(), y = r.f32(), z = r.f32(); pts[k] = toThreePosition({ x, y, z }); }
                        const removeFromHead = r.u32();
                        updateTrajectory(id, pts, removeFromHead);
                    } else if (action === 2) {
                        removeTrajectory(id);
                    } else {
                        console.warn('Unknown trajectory action', action);
                    }
                }
            }

            function decodeCommandBuffer(buf) {
                const r = new Reader(buf);
                try {
                    decodeHeader(r);
                    decodeRegistrations(r);
                    decodeObjects(r);
                    decodeTrajectories(r);
                } catch (e) {
                    console.error('decodeCommandBuffer failed:', e);
                }
            }

            window.startSceneSocket = function (url) {
                const wsScheme = location.protocol === "https:" ? "wss" : "ws";
                const basePath  = location.pathname.replace(/\/[^/]*$/, "/"); // ensure folder path if needed
                url = `${wsScheme}://${location.host}${basePath}${url}`;

                console.log("Starting SceneSocket:", url);
                const ws = new WebSocket(url);
                ws.binaryType = "arraybuffer";

                ws.onopen = () => {
                    console.log("SceneSocket open:", url);
                    ws.send("STARTED");
                }
                ws.onmessage = ev => {
                    if (ev.data instanceof ArrayBuffer) {
                        decodeCommandBuffer(ev.data);
                    }
                    else {
                        console.log("WebSocket message is not ArrayBuffer, ev.data:", ev.data);
                    }
                };
                ws.onclose = () => console.log("SceneSocket closed");
            };

            function attemptStartSceneSocket() {
                if (window.sceneSocketURL) window.startSceneSocket(window.sceneSocketURL);
                else setTimeout(attemptStartSceneSocket, 100);
            }
            attemptStartSceneSocket();

        } catch (e) {
            console.error("three.js bootstrap failed:", e);
        }
    })();
})();
