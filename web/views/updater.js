import { el } from './dom.js';

export function mountUpdater({ apiSend, announce }) {
    const banner = el('div', {
        class: 'update-banner',
        id: 'update-banner',
        role: 'region',
        'aria-label': 'Application update notification',
        hidden: true,
    });
    const header = document.querySelector('header.app-chrome');
    if (header && header.parentNode) {
        header.parentNode.insertBefore(banner, header.nextSibling);
    } else {
        document.body.prepend(banner);
    }

    const notesDialog = buildNotesDialog();
    let currentStatus = null;
    let downloading = false;
    let sessionDismissedReady = false;
    let bootChecked = false;

    function buildNotesDialog() {
        const dialog = el('dialog', {
            class: 'update-notes-dialog',
            'aria-labelledby': 'update-notes-title',
        });
        const head = el('div', { class: 'dialog-head' });
        const title = el('h2', { id: 'update-notes-title', text: 'Release Notes' });
        const closeBtn = el('button', {
            class: 'btn',
            type: 'button',
            text: 'Close',
            title: 'Close release notes',
        });
        closeBtn.addEventListener('click', () => dialog.close());
        head.append(title, closeBtn);

        const body = el('div', { class: 'update-notes-body' });
        const meta = el('p', { class: 'update-notes-meta', 'data-notes': 'meta' });
        const pre = el('pre', { class: 'update-notes-pre', 'data-notes': 'body' });
        body.append(meta, pre);

        const foot = el('div', { class: 'update-notes-foot' });
        const downloadBtn = el('button', {
            class: 'btn btn-primary',
            type: 'button',
            'data-notes': 'download',
            text: 'Download Update',
        });
        downloadBtn.addEventListener('click', () => {
            dialog.close();
            void startDownload();
        });
        const doneBtn = el('button', {
            class: 'btn',
            type: 'button',
            text: 'Close',
        });
        doneBtn.addEventListener('click', () => dialog.close());
        foot.append(downloadBtn, doneBtn);

        dialog.append(head, body, foot);
        document.body.append(dialog);
        return dialog;
    }

    function openNotesModal(rel) {
        if (!rel) return;
        const ver = String(rel.version || '').replace(/^v/i, '');
        notesDialog.querySelector('#update-notes-title').textContent =
            rel.name || `ClassicCars Studio v${ver}`;
        const metaParts = [`Version v${ver}`];
        if (rel.publishedAt) metaParts.push(`Published ${rel.publishedAt.slice(0, 10)}`);
        if (rel.assetName) metaParts.push(`Package: ${rel.assetName}`);
        notesDialog.querySelector('[data-notes="meta"]').textContent = metaParts.join(' · ');
        notesDialog.querySelector('[data-notes="body"]').textContent =
            (rel.notes || rel.body || 'No release notes provided for this version.').trim();
        const dlBtn = notesDialog.querySelector('[data-notes="download"]');
        if (dlBtn) {
            dlBtn.hidden = Boolean(currentStatus?.readyToApply && currentStatus?.stagedVersion === ver);
            dlBtn.disabled = downloading;
        }
        if (!notesDialog.open) notesDialog.showModal();
    }

    function renderBanner({ forceShow = false, errorText = '' } = {}) {
        banner.replaceChildren();
        if (errorText) {
            banner.dataset.state = 'error';
            banner.hidden = false;
            const main = el('div', { class: 'update-banner-main' });
            main.append(
                el('span', { class: 'update-banner-badge', text: 'Update Error' }),
                el('p', { class: 'update-banner-text', text: errorText }),
            );
            const actions = el('div', { class: 'update-banner-actions' });
            const closeBtn = el('button', { class: 'btn', type: 'button', text: 'Dismiss' });
            closeBtn.addEventListener('click', () => { banner.hidden = true; });
            actions.append(closeBtn);
            banner.append(main, actions);
            return;
        }

        if (!currentStatus) {
            banner.hidden = true;
            return;
        }

        const stagedVer = String(currentStatus.stagedVersion || '').replace(/^v/i, '');
        if (currentStatus.readyToApply && stagedVer && (!sessionDismissedReady || forceShow)) {
            banner.dataset.state = 'ready';
            banner.hidden = false;
            const main = el('div', { class: 'update-banner-main' });
            main.append(
                el('span', { class: 'update-banner-badge', text: `v${stagedVer} Ready` }),
                el('p', {
                    class: 'update-banner-text',
                    text: `Update v${stagedVer} is downloaded and ready — Restart ClassicCars Studio whenever you're ready to apply it.`,
                }),
            );
            const actions = el('div', { class: 'update-banner-actions' });
            if (currentStatus.latestRelease) {
                const notesBtn = el('button', {
                    class: 'btn',
                    type: 'button',
                    text: 'Release Notes',
                    title: `View release notes for v${stagedVer}`,
                });
                notesBtn.addEventListener('click', () => openNotesModal(currentStatus.latestRelease));
                actions.append(notesBtn);
            }
            const dismissBtn = el('button', {
                class: 'btn',
                type: 'button',
                text: 'Dismiss',
                title: 'Hide this notice until next launch',
            });
            dismissBtn.addEventListener('click', () => {
                sessionDismissedReady = true;
                banner.hidden = true;
            });
            actions.append(dismissBtn);
            banner.append(main, actions);
            return;
        }

        const rel = currentStatus.latestRelease;
        const latestVer = String(rel?.version || '').replace(/^v/i, '');
        if (!currentStatus.updateAvailable || !rel || !latestVer) {
            banner.hidden = true;
            return;
        }
        if (currentStatus.isDismissed && !forceShow) {
            banner.hidden = true;
            return;
        }

        banner.dataset.state = downloading ? 'downloading' : 'available';
        banner.hidden = false;
        const main = el('div', { class: 'update-banner-main' });
        main.append(
            el('span', { class: 'update-banner-badge', text: 'Update Available' }),
            el('p', {
                class: 'update-banner-text',
                text: downloading
                    ? `Downloading v${latestVer} and staging update for next restart…`
                    : `ClassicCars Studio v${latestVer} is available (currently v${currentStatus.installedVersion || ''}).`,
            }),
        );

        const actions = el('div', { class: 'update-banner-actions' });
        const notesBtn = el('button', {
            class: 'btn',
            type: 'button',
            text: 'Release Notes',
            title: `Read what changed in v${latestVer}`,
        });
        notesBtn.addEventListener('click', () => openNotesModal(rel));

        const downloadBtn = el('button', {
            class: 'btn btn-primary',
            type: 'button',
            disabled: downloading,
            text: downloading ? `Downloading v${latestVer}…` : 'Download Update',
            title: `Download and stage ClassicCars Studio v${latestVer} (applies on next restart)`,
        });
        downloadBtn.addEventListener('click', () => { void startDownload(); });

        const dismissBtn = el('button', {
            class: 'btn',
            type: 'button',
            disabled: downloading,
            text: 'Dismiss',
            title: `Dismiss notifications for v${latestVer}`,
        });
        dismissBtn.addEventListener('click', () => { void dismissVersion(latestVer); });

        actions.append(notesBtn, downloadBtn, dismissBtn);
        banner.append(main, actions);
    }

    async function startDownload() {
        if (downloading) return;
        const ver = String(currentStatus?.latestRelease?.version || '').replace(/^v/i, '');
        downloading = true;
        renderBanner({ forceShow: true });
        announce?.(`Downloading ClassicCars Studio v${ver || 'update'}…`);
        try {
            const data = await apiSend('POST', '/__studio/api/update/download', {});
            currentStatus = data;
            downloading = false;
            sessionDismissedReady = false;
            renderBanner({ forceShow: true });
            announce?.(`Update v${data.stagedVersion || ver} is downloaded and ready to apply on next restart.`);
        } catch (err) {
            downloading = false;
            renderBanner({ errorText: err?.message || 'Could not download update.' });
            announce?.(err?.message || 'Could not download update.');
        }
    }

    async function dismissVersion(version) {
        banner.hidden = true;
        try {
            const data = await apiSend('POST', '/__studio/api/update/dismiss', { version });
            currentStatus = data;
            announce?.(`Dismissed update notice for v${version}.`);
        } catch {
            /* keep hidden for session */
        }
    }

    async function checkForUpdates({ manual = false } = {}) {
        if (manual) announce?.('Checking GitHub Releases for updates…');
        try {
            const data = await apiSend('POST', '/__studio/api/update/check', {});
            currentStatus = data;
            if (data.readyToApply || data.updateAvailable) {
                sessionDismissedReady = false;
                renderBanner({ forceShow: manual });
                if (manual && data.latestRelease?.version) {
                    announce?.(`ClassicCars Studio v${data.latestRelease.version} is available.`);
                }
            } else {
                renderBanner();
                if (manual) {
                    announce?.(`ClassicCars Studio v${data.installedVersion || ''} is up to date.`);
                }
            }
            return data;
        } catch (err) {
            if (manual) {
                renderBanner({ errorText: err?.message || 'Could not check GitHub Releases.' });
                announce?.(err?.message || 'Could not check GitHub Releases.');
            }
            return null;
        }
    }

    function scheduleStartupCheck(appConfig) {
        if (bootChecked) return;
        bootChecked = true;
        window.setTimeout(async () => {
            try {
                const status = await apiSend('GET', '/__studio/api/update/status');
                currentStatus = status;
                if (status.readyToApply) {
                    renderBanner();
                    return;
                }
                const autoCheck = appConfig?.autoCheckUpdates !== false && status.autoCheckUpdates !== false;
                if (autoCheck) {
                    await checkForUpdates({ manual: false });
                }
            } catch {
                /* ignore background startup check errors */
            }
        }, 1200);
    }

    return {
        checkForUpdates,
        scheduleStartupCheck,
    };
}
