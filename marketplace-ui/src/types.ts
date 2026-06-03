export type PluginCategory = 'device' | 'profile_pack' | 'gcode' | 'ui' | 'misc';
export type PluginKind = 'native' | 'wasm' | 'webview' | 'hybrid' | 'data';

export interface MarketplaceVersion {
  version: string;
  kind: PluginKind;
  provides: string[];
  permissions: string[];
  engine_compat: string;
  url: string;
  sha256: string;
  size_bytes: number;
  published_at: string;
}

export interface MarketplacePlugin {
  id: string;
  name: string;
  description: string;
  category: PluginCategory;
  tags: string[];
  author: string;
  homepage?: string;
  repo?: string;
  license?: string;
  screenshots: string[];
  latest_version: string;
  versions: MarketplaceVersion[];
}

export interface MarketplaceCatalog {
  revision_sha256: string;
  generated_at: string;
  plugins: MarketplacePlugin[];
}

export interface InstalledPluginInfo {
  id: string;
  name: string;
  version: string;
  author: string;
  description: string;
  permissions: string[];
}

export interface InstalledResponse {
  plugins: InstalledPluginInfo[];
}

export type ActionStatus = 'ok' | 'denied' | 'error';

export interface ActionResult {
  status: ActionStatus;
  message?: string;
}

export interface OrcaMarketplaceBridge {
  list(): Promise<MarketplaceCatalog>;
  installed(): Promise<InstalledResponse>;
  install(id: string, version: string): Promise<ActionResult>;
  update(id: string, version: string): Promise<ActionResult>;
  uninstall(id: string): Promise<ActionResult>;
  open_external(url: string): Promise<void>;
}

declare global {
  interface Window {
    orca?: {
      marketplace?: OrcaMarketplaceBridge;
    };
  }
}
