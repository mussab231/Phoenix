const HOST = "com.phoenix.host";

function notify(title, message) {
  if (chrome.notifications) {
    chrome.notifications.create({
      type: "basic",
      iconUrl: chrome.runtime.getURL("icon.png"),
      title: title,
      message: message
    });
  }
  console.log("[Phoenix] " + message);
}

function sendToPhoenix(message) {
  chrome.runtime.sendNativeMessage(HOST, message, (resp) => {
    const lastError = chrome.runtime.lastError;
    if (lastError) {
      notify("Phoenix", "Could not reach Phoenix: " + lastError.message);
      return;
    }
    if (resp && resp.ok) {
      notify("Phoenix", resp.method === "handoff"
        ? "Added to the open Phoenix window."
        : "Queued in Phoenix.");
    } else {
      notify("Phoenix", "Phoenix said: " + (resp && resp.error ? resp.error : "unknown error"));
    }
  });
}

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.create({
    id: "send-link-to-phoenix",
    title: "Send link to Phoenix",
    contexts: ["link"]
  });
  chrome.contextMenus.create({
    id: "toggle-intercept",
    title: "Catch downloads automatically",
    contexts: ["action"]
  });
  refreshToggleMenuItem();
});

chrome.action.onClicked.addListener((tab) => {
  if (tab && tab.url && /^https?:/.test(tab.url)) {
    sendToPhoenix({ type: "add", url: tab.url });
  }
});

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (msg && msg.type === "status") {
    chrome.runtime.sendNativeMessage(HOST, { type: "status" }, (resp) => {
      sendResponse(resp || { ok: false, error: chrome.runtime.lastError?.message || "no reply" });
    });
    return true; // keep the message channel open for the async reply
  }
});

// --- Automatic download interception --------------------------------------
// When enabled, every download the browser is about to start is cancelled and
// handed to Phoenix instead, so a normal click on a download link reaches the
// manager without the context menu. Off by default: the user opts in from the
// menu, and any failure (Phoenix not running) falls back to the browser.
const INTERCEPT_KEY = "interceptDownloads";

async function interceptEnabled() {
  try {
    const v = await chrome.storage.local.get(INTERCEPT_KEY);
    return v[INTERCEPT_KEY] === true;
  } catch {
    return false;
  }
}

chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (info.menuItemId === "send-link-to-phoenix" && info.linkUrl) {
    const fileName = /([^/?#]+)(?:[?#]|$)/.exec(info.linkUrl)?.[1] || "";
    sendToPhoenix({ type: "add", url: info.linkUrl, fileName });
    return;
  }
  if (info.menuItemId === "toggle-intercept") {
    interceptEnabled().then((on) => {
      chrome.storage.local.set({ [INTERCEPT_KEY]: !on });
      notify(
        "Phoenix",
        on ? "Download catching OFF" : "Download catching ON — clicks go to Phoenix"
      );
      refreshToggleMenuItem();
    });
  }
});

function refreshToggleMenuItem() {
  interceptEnabled().then((on) => {
    chrome.contextMenus.update("toggle-intercept", {
      title: on
        ? "✓ Catch downloads automatically"
        : "Catch downloads automatically"
    });
  });
}

chrome.downloads.onCreated.addListener((item) => {
  interceptEnabled().then((on) => {
    if (!on || !item || !item.url) return;
    // Cancel the browser download first so the bytes are not fetched twice.
    chrome.downloads.cancel(item.id, () => {
      sendToPhoenix({ type: "add", url: item.url, fileName: item.filename || "" });
    });
  });
});