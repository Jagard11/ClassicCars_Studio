export const CONFIG_PAGE_PATH = '/__studio/config.json';

const config = {
    googleMeasurementId: '',
    extremeLogin: '',
    extremeTrackerHost: '',
};

export function getStudioConfig() {
    return config;
}

export function applyStudioConfig(raw) {
    if (!raw || typeof raw !== 'object') return config;
    const src = raw.config && typeof raw.config === 'object' ? raw.config : raw;
    if (typeof src.googleMeasurementId === 'string') config.googleMeasurementId = src.googleMeasurementId.trim();
    if (typeof src.extremeLogin === 'string') config.extremeLogin = src.extremeLogin.trim();
    if (typeof src.extremeTrackerHost === 'string') config.extremeTrackerHost = src.extremeTrackerHost.trim();
    return config;
}
