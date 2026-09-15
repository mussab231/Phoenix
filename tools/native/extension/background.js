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
});

chrome.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId === "send-link-to-phoenix" && info.linkUrl) {
    const fileName = /([^/?#]+)(?:[?#]|$)/.exec(info.linkUrl)?.[1] || "";
    sendToPhoenix({ type: "add", url: info.linkUrl, fileName });
  }
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