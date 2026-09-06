/* Send the File directly; the firmware endpoint does not parse multipart. */
function uploadFirmware(file, password, onProgress) {
    return new Promise((resolve, reject) => {
        const request = new XMLHttpRequest();
        request.open("POST", "/ota/update");
        request.timeout = 180000;
        request.setRequestHeader("Content-Type", "application/octet-stream");
        if (password) request.setRequestHeader("X-OTA-Password", password);
        request.upload.onprogress = event => {
            if (event.lengthComputable) onProgress(Math.round(event.loaded * 100 / event.total));
        };
        request.onload = () => request.status >= 200 && request.status < 300
            ? resolve() : reject(new Error(request.responseText || "Upload failed (" + request.status + ")."));
        request.onerror = () => reject(new Error("Connection lost. Check the panel before retrying."));
        request.ontimeout = () => reject(new Error("Upload timed out. Check the panel before retrying."));
        request.send(file);
    });
}
if (typeof document !== "undefined") {
    document.getElementById("upload").addEventListener("submit", async event => {
        event.preventDefault();
        const button = document.getElementById("submit");
        const status = document.getElementById("status");
        const file = document.getElementById("firmware").files[0];
        if (!file) return;
        if (/merged/i.test(file.name)) {
            status.textContent = "Choose tempest_display.bin, not the merged USB image.";
            return;
        }
        button.disabled = true;
        status.textContent = "Uploading...";
        try {
            await uploadFirmware(file, document.getElementById("password").value,
                percent => { document.getElementById("progress").value = percent; });
            status.textContent = "Update written. The panel is restarting.";
        } catch (error) {
            status.textContent = error.message;
            button.disabled = false;
        }
    });
}
if (typeof module !== "undefined") module.exports = { uploadFirmware };
