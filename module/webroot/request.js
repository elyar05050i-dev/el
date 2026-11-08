const RequestApi = (() => {
  class ApiError extends Error {
    constructor(message, details = {}) {
      super(message || details.code || "Request failed");
      this.name = "ApiError";
      this.status = details.status || 0;
      this.code = details.code || "REQUEST_FAILED";
      this.errno = details.errno || 0;
      this.requestId = details.requestId || "";
      this.data = details.data;
    }
  }

  let sessionPromise = null;
  let rpcSpoolDir = "";
  const rpcClient = "$MODDIR/bin/susfs_ksu_rpc";

  function execPromise(cmd, input = "") {
    return new Promise((resolve, reject) => {
      if (typeof ksu === "undefined" || typeof ksu.exec !== "function") {
        reject(new ApiError("KernelSU JS interface not available", { code: "KSU_UNAVAILABLE" }));
        return;
      }
      const callbackName = "ksu_cb_" + Date.now() + "_" + Math.random().toString(36).slice(2);
      window[callbackName] = (errno, stdout, stderr) => {
        delete window[callbackName];
        if (errno !== 0) {
          reject(new ApiError(stderr || "ksu.exec failed", { code: "KSU_EXEC_FAILED", errno }));
        } else {
          resolve({ stdout, stderr });
        }
      };
      try {
        ksu.exec(cmd, input, callbackName);
      } catch (error) {
        delete window[callbackName];
        reject(new ApiError(error?.message || "ksu.exec threw", { code: "KSU_EXEC_THROW" }));
      }
    });
  }

  async function rpcPost(path, payload = {}) {
    const body = JSON.stringify(payload);
    // Use stdin for large payloads to avoid shell argument limits and encoding issues
    const json = JSON.stringify({ path, body });
    const cmd = rpcClient + " /data/adb/susfs_ksu/susfs/rpc.sock";
    const result = await execPromise(cmd, json);
    let parsed;
    try { parsed = JSON.parse(result.stdout); } catch (_) {
      throw new ApiError("Invalid RPC response", { code: "INVALID_RPC_RESPONSE" });
    }
    if (parsed.status !== 200) {
      let message = "RPC error", code = "RPC_ERROR", errno = 0, data = null;
      try {
        const env = JSON.parse(parsed.body);
        message = env.message || message; code = env.code || code; errno = env.errno || errno; data = env.data;
      } catch (_) {}
      throw new ApiError(message, { status: parsed.status, code, errno, data });
    }
    let env;
    try { env = JSON.parse(parsed.body); } catch (_) {
      throw new ApiError("Invalid RPC envelope", { code: "INVALID_RPC_ENVELOPE" });
    }
    if (!env.ok) {
      throw new ApiError(env.message || "API error", { status: 200, code: env.code, errno: env.errno, data: env.data });
    }
    if (env.data && typeof env.data === "object" && !Array.isArray(env.data) && env.requestId && !env.data.requestId) {
      Object.defineProperty(env.data, "requestId", { value: env.requestId, enumerable: false, configurable: true });
    }
    return env.data;
  }

  async function initSession(force = false) {
    if (force) sessionPromise = null;
    if (!sessionPromise) {
      sessionPromise = (async () => {
        const data = await rpcPost("/api/session", {});
        if (!data?.token) throw new ApiError("Session token is missing", { code: "INVALID_SESSION" });
        return data.token;
      })().catch(error => { sessionPromise = null; throw error; });
    }
    return sessionPromise;
  }

  async function authorizedFetch(path, payload = {}, retry = true) {
    if (window.SusfsLifecycle?.closed && path !== '/api/susfs/webui/exit')
      throw new ApiError('WebUI 已关闭，请从 KernelSU/KernelSU Next 重新打开', { code: 'WEBUI_CLOSED' });
    await initSession();
    const data = await rpcPost(path, payload);
    return data;
  }

  async function postApi(path, payload = {}, options = {}) {
    return authorizedFetch(path, payload);
  }

  async function postText(path, bodyText = "", options = {}) {
    return authorizedFetch(path, { text: bodyText });
  }

  function decodeCppUrl(str) {
    if (typeof str !== 'string') return str;
    return decodeURIComponent(str.replace(/\+/g, ' '));
  }

  // Terminal/Command related APIs are SKRoot-specific and not available in KSU standalone
  // They are kept as no-op stubs to avoid breaking existing calls from file-manager.js
  async function sendCommand(cmd) {
    return ""; // no-op
  }

  async function getNewOutput() {
    return ""; // no-op
  }

  async function getQuickActions() {
    return null; // no-op
  }

  async function saveAutoTasks(tasks) {
    return ""; // no-op
  }

  async function getHideDir() {
    return ""; // no-op
  }

  async function checkFileType(filePath) {
    return ""; // no-op
  }

  async function checkExecMount(dir) {
    return ""; // no-op
  }

  async function getTerminalKeepMode() {
    return ""; // no-op
  }

  async function saveTerminalKeepMode(enabled) {
    return ""; // no-op
  }

  async function getTerminalTransportMode() {
    return { effective: 'pipe', saved: 'pipe' }; // no-op
  }

  async function saveTerminalTransportMode(mode) {
    return ""; // no-op
  }

  async function exitWebui() {
    return "";
  }

  async function listDir(path) {
    const data = await postApi('/api/files/list', { path });
    return data?.items || [];
  }

  async function getAutoTasks() {
    return []; // no-op
  }

  async function saveAutoTasks(tasks) {
    return ""; // no-op
  }

  async function getHideDir() {
    return ""; // no-op
  }

  async function checkFileType(filePath) {
    return ""; // no-op
  }

  async function checkExecMount(dir) {
    return ""; // no-op
  }

  async function getTerminalKeepMode() {
    return ""; // no-op
  }

  async function saveTerminalKeepMode(enabled) {
    return ""; // no-op
  }

  async function getTerminalTransportMode() {
    return { effective: 'pipe', saved: 'pipe' }; // no-op
  }

  async function saveTerminalTransportMode(mode) {
    return ""; // no-op
  }

  async function exitWebui() {
    return "";
  }

  const getFileSettings = () => postApi('/api/files/settings/get');
  const setFileHome = (pane, path) => postApi('/api/files/settings/set-home', { pane, path });
  const getFilePlaces = () => postApi('/api/files/places/get');
  const setFilePlaces = places => postApi('/api/files/places/set', places);
  const makeDirectory = (path, mode = 0o755) => postApi('/api/files/mkdir', { path, mode });
  const createFile = (path, mode = 0o644) => postApi('/api/files/create', { path, mode });
  const renamePath = (source, target) => postApi('/api/files/rename', { source, target });
  const getFileProperties = path => postApi('/api/files/properties/get', { path });
  const listPrincipals = path => postApi('/api/files/principals/list', { path });
  const startFileJob = spec => postApi('/api/files/job/start', spec);
  const getFileJob = id => postApi('/api/files/job/status', id ? { id } : {});
  const cancelFileJob = id => postApi('/api/files/job/cancel', { id });
  const openText = (path, options = {}) => postApi('/api/files/text/open', { path }, options);
  const readTextChunk = (sessionId, offset, limit = 128 * 1024, options = {}) =>
    postApi('/api/files/text/read', { sessionId, offset, limit }, options);
  const openImage = (path, options = {}) => postApi('/api/files/image/open', { path }, options);
  const readImageChunk = (sessionId, offset, limit = 128 * 1024, options = {}) =>
    postApi('/api/files/image/read', { sessionId, offset, limit }, options);
  const closeImage = sessionId => postApi('/api/files/image/close', { sessionId });
  const openArchive = path => postApi('/api/files/archive/open', { path });
  const getArchiveStatus = sessionId => postApi('/api/files/archive/status', { sessionId });
  const listArchive = (sessionId, innerPath = '', offset = 0, limit = 500) =>
    postApi('/api/files/archive/list', { sessionId, innerPath, offset, limit });
  const closeArchive = sessionId => postApi('/api/files/archive/close', { sessionId });
  const startTextSave = (path, version, size, force = false) =>
    postApi('/api/files/text/save/start', { path, version, size, force });
  const writeTextChunk = (saveId, offset, base64) =>
    postApi('/api/files/text/save/chunk', { saveId, offset, base64 });
  const commitTextSave = (saveId, force = false) =>
    postApi('/api/files/text/save/commit', { saveId, force });
  const abortTextSave = (saveId = "") => postApi('/api/files/text/save/abort', { saveId });
  const getAutomationConfig = () => postApi('/api/automation/config/get');
  const setAutomationConfig = config => postApi('/api/automation/config/set', config);
  const startAutomationRun = payload => postApi('/api/automation/run/start', payload);
  const getAutomationRun = id => postApi('/api/automation/run/status', id ? { id } : {});
  const cancelAutomationRun = id => postApi('/api/automation/run/cancel', { id });
  const susfsApps = () => postApi('/api/susfs/apps');
  const getAppsMetadata = (packages, includeIcons = false, options = {}) =>
    postApi('/api/apps/metadata', { packages, includeIcons }, options);
  const susfsStatus = () => postApi('/api/susfs/status');
  const susfsApply = () => postApi('/api/susfs/apply');
  const susfsGetConfig = () => postApi('/api/susfs/config-get');
  const susfsSaveConfig = config => postApi('/api/susfs/config-save', config);
  const susfsQueryObjects = paths => postApi('/api/susfs/objects-query', { paths });
  const susfsSampleKstat = path => postApi('/api/susfs/kstat-sample', { path });
  const webuiPing = () => postApi('/api/susfs/webui/ping');
  const webuiExit = () => postApi('/api/susfs/webui/exit', {}, {keepalive:true});
  const filesManagerStatus = () => postApi('/api/files/manager/status');

  return {
    webuiPing, webuiExit,
    ApiError,
    initSession,
    sendCommand,
    getNewOutput,
    getQuickActions,
    listDir,
    getAutoTasks,
    saveAutoTasks,
    getHideDir,
    checkFileType,
    checkExecMount,
    getTerminalKeepMode,
    saveTerminalKeepMode,
    getTerminalTransportMode,
    saveTerminalTransportMode,
    exitWebui,
    getFileSettings,
    setFileHome,
    getFilePlaces,
    setFilePlaces,
    makeDirectory,
    createFile,
    renamePath,
    getFileProperties,
    listPrincipals,
    startFileJob,
    getFileJob,
    cancelFileJob,
    openText,
    readTextChunk,
    openImage,
    readImageChunk,
    closeImage,
    openArchive,
    getArchiveStatus,
    listArchive,
    closeArchive,
    startTextSave,
    writeTextChunk,
    commitTextSave,
    abortTextSave,
    getAutomationConfig,
    setAutomationConfig,
    startAutomationRun,
    getAutomationRun,
    cancelAutomationRun,
    susfsApps, getAppsMetadata, susfsStatus, susfsApply, susfsGetConfig, susfsSaveConfig, susfsQueryObjects, susfsSampleKstat,
    filesManagerStatus,
  };
})();