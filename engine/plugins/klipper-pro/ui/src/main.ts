// Klipper-Pro webview entry. Wires the static tabs and subscribes to
// the engine event bus through window.orca.events.

import {
  defineWebviewPlugin,
  type ExportFinishedPayload,
  type SlicingFinishedPayload,
  type SlicingProgressPayload,
} from '@orca/plugin-types';

const tabs = document.querySelectorAll<HTMLButtonElement>('nav button');
const panels = document.querySelectorAll<HTMLElement>('.panel');

function selectTab(name: string): void {
  tabs.forEach(b => b.classList.toggle('active', b.dataset.tab === name));
  panels.forEach(p => p.classList.toggle('active', p.id === name));
}

tabs.forEach(b => {
  b.addEventListener('click', () => selectTab(b.dataset.tab ?? 'dashboard'));
});

function setStatus(id: string, text: string): void {
  const el = document.getElementById(id);
  if (el) el.textContent = text;
}

const plugin = defineWebviewPlugin({
  id:      'com.orcaslicer.klipper-pro',
  name:    'Klipper-Pro',
  version: '0.1.0',

  async onMount() {
    await window.orca.log(2 /* info */, 'klipper-pro UI mounted');

    window.orca.events.on('SlicingProgress',
      ({ handle, progress, message }: SlicingProgressPayload) => {
        setStatus('progress',
          `slice #${handle} — ${(progress * 100).toFixed(0)}% ${message}`);
      });

    window.orca.events.on('SlicingFinished',
      ({ handle, success, error }: SlicingFinishedPayload) => {
        setStatus('last-slice', success
          ? `slice #${handle} done`
          : `slice #${handle} failed: ${error}`);
        setStatus('progress', success ? 'idle' : 'error');
      });

    window.orca.events.on('ExportFinished',
      ({ handle, success, line_count, error }: ExportFinishedPayload) => {
        setStatus('last-export', success
          ? `export #${handle} done (${line_count} lines)`
          : `export #${handle} failed: ${error}`);
      });
  },
});

plugin.mount();
