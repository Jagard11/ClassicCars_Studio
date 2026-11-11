export const CONFIG_PAGE_PATH = '/__studio/config.json';

const config = {
    brandName: '',
    siteOrigin: '',
    siteHost: '',
    siteHosts: [],
    defaultAuthor: '',
    defaultWebsiteRoot: '',
    googleMeasurementId: '',
    extremeLogin: '',
    extremeTrackerHost: '',
    trafficExcludeInclude: '/includes/traffic_exclude.php',
};

export function getStudioConfig() {
    return config;
}

export function applyStudioConfig(raw) {
    if (!raw || typeof raw !== 'object') return config;
    const src = raw.config && typeof raw.config === 'object' ? raw.config : raw;
    if (typeof src.brandName === 'string') config.brandName = src.brandName.trim();
    if (typeof src.siteOrigin === 'string') config.siteOrigin = src.siteOrigin.trim().replace(/\/+$/, '');
    if (typeof src.siteHost === 'string') config.siteHost = src.siteHost.trim();
    if (Array.isArray(src.siteHosts)) {
        config.siteHosts = src.siteHosts
            .filter((item) => typeof item === 'string' && item.trim())
            .map((item) => item.trim().toLowerCase());
    }
    if (typeof src.defaultAuthor === 'string') config.defaultAuthor = src.defaultAuthor.trim();
    if (typeof src.defaultWebsiteRoot === 'string') config.defaultWebsiteRoot = src.defaultWebsiteRoot.trim();
    if (typeof src.googleMeasurementId === 'string') config.googleMeasurementId = src.googleMeasurementId.trim();
    if (typeof src.extremeLogin === 'string') config.extremeLogin = src.extremeLogin.trim();
    if (typeof src.extremeTrackerHost === 'string' && src.extremeTrackerHost.trim()) {
        config.extremeTrackerHost = src.extremeTrackerHost.trim();
    }
    if (typeof src.trafficExcludeInclude === 'string' && src.trafficExcludeInclude.trim()) {
        config.trafficExcludeInclude = src.trafficExcludeInclude.trim();
    }
    return config;
}

export function isSiteHost(hostname) {
    const clean = String(hostname || '').toLowerCase().replace(/^www\./, '');
    if (!clean) return false;
    if (config.siteHost && clean === config.siteHost.toLowerCase().replace(/^www\./, '')) return true;
    if (config.siteOrigin) {
        try {
            const originHost = new URL(config.siteOrigin).hostname.toLowerCase().replace(/^www\./, '');
            if (clean === originHost) return true;
        } catch { /* ignore invalid origin URL */ }
    }
    return config.siteHosts.some((host) => host.replace(/^www\./, '') === clean);
}
