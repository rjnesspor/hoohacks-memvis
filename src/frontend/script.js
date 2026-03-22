const fileInput = document.getElementById('input_file');

let heapData = [];
let heapChart = null;
let leakedPtrs = new Set();

function readFileAsText(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = e => resolve(e.target.result);
        reader.onerror = () => reject(reader.error);
        reader.readAsText(file);
    });
}

async function loadStuff() {
    const inputFile = fileInput.files[0];

    if (!inputFile) return alert(`You haven't input a file. Try again.`);

    const text = await readFileAsText(inputFile);
    const parsed = JSON.parse(text);

    const traceEvents = parsed["traceEvents"];
    const stackEvents = parsed["stackMemory"];
    heapData = parsed["heapMemory"];

    heapChart = constructHeapGraph(heapData);
    const timeline = drawTimelineGraph(traceEvents);

    constructStackGraph(stackEvents);

    heapChart.data.datasets[0]._origColors = [...heapChart.data.datasets[0].pointBackgroundColor];
    timeline.linkHeapChart(heapChart, heapData);

    // document.getElementById('timeline_graph').scrollIntoView({ behavior: 'smooth' });
    leakedPtrs = updateInfoBox(heapData);
}

function extractStackData(data) {
    const functionDeltas = {};
    const enterMap = {};

    for (const event of data) {
        if (event.event === "function enter") {
            if (event.name === "main") {
                functionDeltas["libc"] = event.size;
            }
            enterMap[event.id] = event;
        } else if (event.event === "function exit") {
            const enter = enterMap[event.id];
            if (!enter) continue;

            const delta = Math.max(0, event.size - enter.size);

            if (!functionDeltas[event.name]) {
                functionDeltas[event.name] = 0;
            }
            functionDeltas[event.name] += delta;

            delete enterMap[event.id];
        }
    }

    console.log(functionDeltas);

    const labels = Object.keys(functionDeltas);
    const parsedData = labels.map(name => functionDeltas[name]);

    // Sort both arrays together by value, descending
    const sorted = labels
        .map((label, i) => ({ label, value: parsedData[i] }))
        .sort((a, b) => b.value - a.value);

    return [sorted.map(x => x.value), sorted.map(x => x.label)];
}

function constructStackGraph(data) {
    const stackGraph = document.getElementById('stack_graph');
    let [stackData, labels] = extractStackData(data);

    let dataset = {
        label: "Program Stack Growth",
        data: stackData,
        backgroundColor: '#378ADD',
        borderColor: '#4a5068',
        borderWidth: 1,
    }

    new Chart(stackGraph, {
        type: 'bar',
        data: {
            labels: labels,
            datasets: [dataset]
        },
        options: {
            scales: {
                x: {
                    grid: {
                        color: '#2e3348',
                    },
                    ticks: {
                        color: '#8b90a7',
                    }
                },
                y: {
                    grid: {
                        color: '#2e3348',
                    },
                    ticks: {
                        color: '#8b90a7',
                    }
                }
            },
            plugins: {
                tooltip: {
                    displayColors: false,
                    bodyColor: 'white',
                    titleColor: 'white',
                    callbacks: {
                        label: function (tooltipItem) {
                            return `Stack Growth: ${formatBytes(tooltipItem.raw)}`;
                        }
                    }
                }
            }
        }
    });
}

function extractHeapData(data) {
    let delta = [0];
    let labels = [data[0]?.ts ?? 0];
    let pointColors = ['gray'];
    let knownPointers = [];
    for (let i = 0; i < data.length; i++) {
        switch (data[i].event) {
            case "malloc":
                delta.push(delta[i] + data[i].size);
                knownPointers.push({ ptr: data[i].ptr, size: data[i].size });
                pointColors.push('#378ADD');
                break;
            case "calloc":
                delta.push(delta[i] + data[i].size);
                knownPointers.push({ ptr: data[i].ptr, size: data[i].size });
                pointColors.push('#1D9E75');
                break;
            case "realloc":
                let oldptr = knownPointers.find(p => p.ptr === data[i].old_ptr);
                if (!oldptr) {
                    delta.push(delta[i] + data[i].size);
                    knownPointers.push({ ptr: data[i].ptr, size: data[i].size });
                } else {
                    let dx = data[i].size - oldptr.size;
                    delta.push(delta[i] + dx);
                    oldptr.ptr = data[i].ptr;
                    oldptr.size = data[i].size;
                }
                pointColors.push('#EF9F27');
                break;
            case "free":
                let idx = knownPointers.findIndex(p => p.ptr === data[i].ptr);
                if (idx === -1) {
                    delta.push(delta[i]);
                } else {
                    delta.push(delta[i] - knownPointers[idx].size);
                    knownPointers.splice(idx, 1);
                }
                pointColors.push('#E24B4A');
                break;
        }
        labels.push(data[i].ts);
    }
    return [delta, labels, pointColors];
}

function constructHeapGraph(data) {
    const heapGraph = document.getElementById('heap_graph');
    let [delta, labels, pointColors] = extractHeapData(data);

    let dataset = {
        label: "Program Heap Allocations",
        data: delta,
        fill: false,
        borderColor: '#4a5068',
        pointBorderWidth: 0,
        tension: 0.1,
        pointBackgroundColor: pointColors,
        pointRadius: 5,
        pointHoverRadius: 6,
    }

    const tMin = labels[0];

    const chart = new Chart(heapGraph, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [dataset]
        },
        options: {
            layout: {
                padding: {
                    right: 10,
                }
            },
            scales: {
                x: {
                    grid: {
                        color: '#2e3348',
                    },
                    offset: false,
                    ticks: {
                        color: '#8b90a7',
                        maxTicksLimit: 7,
                        callback: function (val, index) {
                            const ts = labels[index];
                            if (ts === undefined) return '';
                            const elapsed = ts - tMin;
                            return elapsed >= 1000
                                ? (elapsed / 1000).toFixed(1) + 'ms'
                                : elapsed.toFixed(0) + 'μs';
                        }
                    }
                },
                y: {
                    grid: {
                        color: '#2e3348',
                    },
                    ticks: {
                        color: '#8b90a7',
                    },
                },
            },
            plugins: {
                tooltip: {
                    displayColors: false,
                    callbacks: {
                        title: function (tooltipItems) {
                            const i = tooltipItems[0].dataIndex;
                            if (i === 0) return "Initial State";
                            const event = data[i - 1];
                            return event.event;
                        },
                        label: function (tooltipItem) {
                            const i = tooltipItem.dataIndex;
                            const memSize = tooltipItem.raw;
                            if (i === 0) return `Memory: ${formatBytes(memSize)}`;
                            const event = data[i - 1];
                            return [
                                `Pointer: ${event.ptr}`,
                                `Size: ${formatBytes(event.size)}`,
                                `Total: ${formatBytes(memSize)}`,
                                `Call Stack: ${parseReadableStack(event.stack)}`
                            ];
                        }
                    }
                }
            }
        },
        plugins: [{
            id: 'forceLastTick',
            afterUpdate(chart) {
                const xAxis = chart.scales.x;
                const last = labels.length - 1;
                if (xAxis.ticks[xAxis.ticks.length - 1]?.value !== last) {
                    xAxis.ticks.push({
                        value: last,
                        label: (() => {
                            const elapsed = labels[last] - tMin;
                            return elapsed >= 1000
                                ? (elapsed / 1000).toFixed(1) + 'ms'
                                : elapsed.toFixed(0) + 'μs';
                        })()
                    });
                }
            }
        }]
    })
    return chart;

}

function parseReadableStack(stack) {
    return stack
        .filter(frame => !frame.includes('wrap.so') && !frame.includes('libc.so') && !frame.includes('_start'))
        .map(frame => frame.replace(/\s+\[0x[0-9a-f]+\]$/, ''));
}

function formatBytes(bytes) {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

const TIMELINE_COLORS = [
    '#378ADD', '#1D9E75', '#D85A30', '#D4537E', '#BA7517', '#7F77DD',
];

const LANE_H = 22;
const LANE_PAD = 5;
const PAD_LEFT = 110;
const PAD_RIGHT = 16;
const PAD_TOP = 10;
const PAD_BOTTOM = 24;

function drawTimelineGraph(traceEvents) {
    // filter to complete events only
    const events = traceEvents.filter(e => e.ph === 'X');
    const tMin = Math.min(...events.map(e => e.ts));
    const tMax = Math.max(...events.map(e => e.ts + e.dur));

    const fnNames = [...new Set(events.map(e => e.name))]

    const fnColor = {};
    fnNames.forEach((n, i) => { fnColor[n] = TIMELINE_COLORS[i % TIMELINE_COLORS.length]; });

    const canvasHeight = PAD_TOP + fnNames.length * (LANE_H + LANE_PAD) - LANE_PAD + PAD_BOTTOM;

    const canvas = document.getElementById('timeline_graph');
    canvas.height = canvasHeight;
    const ctx = canvas.getContext('2d');

    function xOf(ts) {
        const drawW = canvas.offsetWidth - PAD_LEFT - PAD_RIGHT;
        return PAD_LEFT + ((ts - tMin) / (tMax - tMin)) * drawW;
    }

    let hoveredFn = null;
    let heapChart = null; // populated by linkHeapChart()

    function draw() {
        const W = canvas.offsetWidth;
        const dpr = window.devicePixelRatio || 1;
        canvas.width = W * dpr;
        canvas.height = canvasHeight * dpr;
        canvas.style.height = canvasHeight + 'px';
        ctx.scale(dpr, dpr);
        ctx.clearRect(0, 0, W, canvasHeight);

        const drawW = W - PAD_LEFT - PAD_RIGHT;

        // tick labels along bottom
        ctx.font = '11px system-ui, sans-serif';
        ctx.fillStyle = '#8b90a7';
        ctx.textAlign = 'center';
        const tickCount = 6;
        for (let t = 0; t <= tickCount; t++) {
            const ts = tMin + (t / tickCount) * (tMax - tMin);
            const x = PAD_LEFT + (t / tickCount) * drawW;
            const label = (ts - tMin) >= 1000
                ? ((ts - tMin) / 1000).toFixed(1) + 'ms'
                : ((ts - tMin)).toFixed(0) + 'μs';
            ctx.fillText(label, x, canvasHeight - 6);
            ctx.beginPath();
            ctx.strokeStyle = '#2e3348';
            ctx.lineWidth = 0.5;
            ctx.moveTo(x, PAD_TOP);
            ctx.lineTo(x, canvasHeight - PAD_BOTTOM + 4);
            ctx.stroke();
        }

        fnNames.forEach((lane, li) => {
            const y = PAD_TOP + li * (LANE_H + LANE_PAD);

            // lane label
            ctx.font = '11px system-ui, sans-serif';
            ctx.fillStyle = '#8b90a7';
            ctx.textAlign = 'right';
            ctx.textBaseline = 'middle';
            ctx.fillText(lane, PAD_LEFT - 8, y + LANE_H / 2);

            events.filter(e => e.name === lane).forEach(ev => {
                const x1 = xOf(ev.ts);
                const x2 = xOf(ev.ts + ev.dur);
                const bw = Math.max(x2 - x1, 2);
                const color = fnColor[lane];

                let alpha = hoveredFn ? (hoveredFn.name === lane ? 1 : 0.25) : 1;
                ctx.globalAlpha = alpha;
                ctx.fillStyle = color;
                ctx.beginPath();
                ctx.roundRect(x1, y, bw, LANE_H, 3);
                ctx.fill();
                ctx.globalAlpha = 1;

                if (bw > 32) {
                    ctx.save();
                    ctx.beginPath();
                    ctx.rect(x1 + 4, y, bw - 8, LANE_H);
                    ctx.clip();
                    ctx.fillStyle = '#fff';
                    ctx.font = '10px system-ui, sans-serif';
                    ctx.textAlign = 'left';
                    ctx.textBaseline = 'middle';
                    const durLabel = ev.dur >= 1000
                        ? (ev.dur / 1000).toFixed(1) + 'ms'
                        : ev.dur + 'μs';
                    ctx.fillText(durLabel, x1 + 5, y + LANE_H / 2);
                    ctx.restore();
                }
            });
        });
    }

    canvas.addEventListener('mousemove', (e) => {
        if (document.getElementById('leak-mode-toggle').checked) return;
        const rect = canvas.getBoundingClientRect();
        const mx = e.clientX - rect.left;
        const my = e.clientY - rect.top;
        const W = canvas.offsetWidth;
        const drawW = W - PAD_LEFT - PAD_RIGHT;
        if (mx < PAD_LEFT) { if (hoveredFn) { hoveredFn = null; draw(); syncHeap(); } return; }

        const ts = tMin + ((mx - PAD_LEFT) / drawW) * (tMax - tMin);
        let found = null;
        fnNames.forEach((lane, li) => {
            const y = PAD_TOP + li * (LANE_H + LANE_PAD);
            if (my >= y && my <= y + LANE_H) {
                const ev = events.find(ev => ev.name === lane && ts >= ev.ts && ts <= ev.ts + ev.dur);
                if (ev) found = { name: lane, ts: ev.ts, dur: ev.dur, id: ev.id };
            }
        });

        if (JSON.stringify(found) !== JSON.stringify(hoveredFn)) {
            hoveredFn = found;
            draw();
            syncHeap();
        }
    });

    canvas.addEventListener('mouseleave', () => {
        if (document.getElementById('leak-mode-toggle').checked) return;
        hoveredFn = null;
        draw();
        syncHeap();
    });

    function linkHeapChart(chartInstance, allocData) {
        heapChart = { chart: chartInstance, data: allocData };

        chartInstance.config.options.onHover = (evt, elements) => {
            if (elements.length === 0) {
                if (hoveredFn) { hoveredFn = null; draw(); }
                return;
            }
            const i = elements[0].index;
            if (i === 0) return; // Initial State — no alloc entry
            const alloc = allocData[i - 1]; // ← was: allocData[elements[0].index]
            const active = events.find(e => e.id === alloc.function_id);
            const next = active ? { name: active.name, ts: active.ts, dur: active.dur, id: active.id } : null;
            if (JSON.stringify(next) !== JSON.stringify(hoveredFn)) {
                hoveredFn = next;
                draw();
            }
        };
    }

    function syncHeap() {
        if (!heapChart) return;
        const { chart, data } = heapChart;
        const ds = chart.data.datasets[0];
        ds.pointBackgroundColor = [ds._origColors[0], ...data.map((a, i) => {
            if (!hoveredFn) return ds._origColors[i + 1];
            return a.id === hoveredFn.id
                ? fnColor[hoveredFn.name]
                : 'rgba(180,180,180,0.3)';
        })];
        chart.update('none');
    }


    new ResizeObserver(() => draw()).observe(canvas.parentElement);
    requestAnimationFrame(() => draw());

    return { linkHeapChart };
}

function updateInfoBox(data) {
    const box = document.getElementById('info-box');

    const totalEl = document.getElementById('info-total');
    const allocCountEl = document.getElementById('info-alloc-count');

    const leakedEl = document.getElementById('info-leaked');
    const countEl = document.getElementById('info-leak-count');
    const toggle = document.getElementById('leak-mode-toggle');

    const totalAllocCount = data.filter(e => e.event === 'malloc' || e.event === 'calloc').length;
    const totalAllocSize = data.filter(e => e.event === 'malloc' || e.event === 'calloc').reduce((sum, e) => sum + e.size, 0);

    const live = {};
    data.forEach(e => {
        if (e.event === 'malloc' || e.event === 'calloc') live[e.ptr] = e.size;
        else if (e.event === 'realloc') { delete live[e.old_ptr]; live[e.ptr] = e.size; }
        else if (e.event === 'free') delete live[e.ptr];
    });

    const leaks = Object.values(live);
    const totalLeaked = leaks.reduce((a, b) => a + b, 0);

    leakedEl.textContent = formatBytes(totalLeaked);
    countEl.textContent = leaks.length;

    totalEl.textContent = formatBytes(totalAllocSize);
    allocCountEl.textContent = totalAllocCount;

    toggle.addEventListener('change', () => {
        document.dispatchEvent(new CustomEvent('leakModeChanged', { detail: { enabled: toggle.checked } }));
    });

    return new Set(Object.keys(live));
}

document.addEventListener('leakModeChanged', (e) => {
    const enabled = e.detail.enabled;
    const ds = heapChart.data.datasets[0];

    if (!enabled) {
        ds.pointBackgroundColor = [...ds._origColors];
    } else {
        ds.pointBackgroundColor = heapData.map((event, i) => {
            if (leakedPtrs.has(event.ptr)) return '#E24B4A';
            return 'rgba(180,180,180,0.3)';
        });
        ds.pointBackgroundColor.unshift('gray');
    }

    heapChart.update('none');
});