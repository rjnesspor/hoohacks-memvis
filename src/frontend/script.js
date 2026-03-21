const fileInput = document.getElementById('input_file');

fileInput.addEventListener('change', (event) => {
    console.log("found!");
    const file = event.target.files[0];

    if (file) {
        const reader = new FileReader();
        reader.onload = function (e) {
            const lines = e.target.result.split('\n');
            const results = [];
            const errors = [];

            lines.forEach((line, i) => {
                const trimmed = line.trim();
                if (!trimmed) return;

                try {
                    results.push(JSON.parse(trimmed));
                } catch (err) {
                    errors.push({ line: i + 1, content: trimmed, error: err.message });
                }
            });

            console.log('Parsed objects:', results);
            if (errors.length) console.warn('Parse errors:', errors);

            constructHeapGraph(results);
        };

        reader.onerror = function () {
            console.error('Failed to read file:', reader.error);
        };

        reader.readAsText(file);
    }


})

function extractData(data) {
    let delta = [0];
    let labels = [1];
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
                let dx = data[i].size - oldptr.size;
                delta.push(delta[i] + dx);

                oldptr.ptr = data[i].ptr;
                oldptr.size = data[i].size;

                pointColors.push('#EF9F27');
                break;
            case "free":
                let idx = knownPointers.findIndex(p => p.ptr === data[i].ptr);
                delta.push(delta[i] - knownPointers[idx].size);
                knownPointers.splice(idx, 1);
                pointColors.push('#E24B4A');
                break;                
        }
        labels.push(i + 2);
    }
    return [delta, labels, pointColors];
}

function constructHeapGraph(data) {
    const heapGraph = document.getElementById('heap_graph');
    let [delta, labels, pointColors] = extractData(data);
    
    let dataset = {
        label: "Program Heap Allocations",
        data: delta,
        fill: false,
        borderColor: 'black',
        pointBorderWidth: 0,
        tension: 0.1,
        pointBackgroundColor: pointColors,
        pointRadius: 6,
        pointHoverRadius: 7,
    }
    
    new Chart(heapGraph, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [dataset]
        },
        options: {
            plugins: {
                tooltip: {
                    displayColors: false,
                    callbacks: {
                        title: function(tooltipItems) {
                            const i = tooltipItems[0].dataIndex;
                            if (i === 0) return "Initial State";
                            const event = data[i - 1];
                            return event.event;
                        },
                        label: function(tooltipItem) {
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
        }
    })
        
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