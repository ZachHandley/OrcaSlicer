import { isNativeBridgeAvailable } from './api/bridge';
import { Chip } from './components/ui/Chip';
import { hrefFor, useHashRoute } from './hooks/useHashRoute';
import { CatalogPage } from './pages/Catalog';
import { InstalledPage } from './pages/Installed';
import { PluginDetailPage } from './pages/PluginDetail';

interface NavLink {
  href: string;
  label: string;
  active: boolean;
}

function navLinks(active: 'catalog' | 'installed' | 'other'): NavLink[] {
  return [
    { href: hrefFor({ kind: 'catalog' }), label: 'Catalog', active: active === 'catalog' },
    { href: hrefFor({ kind: 'installed' }), label: 'Installed', active: active === 'installed' },
  ];
}

export function App() {
  const route = useHashRoute();
  const native = isNativeBridgeAvailable();

  const activeNav: 'catalog' | 'installed' | 'other' =
    route.kind === 'catalog' ? 'catalog' : route.kind === 'installed' ? 'installed' : 'other';

  return (
    <div className="min-h-screen flex flex-col">
      <header className="border-b border-orca-border bg-orca-panel">
        <div className="max-w-5xl mx-auto px-4 py-3 flex items-center gap-4 flex-wrap">
          <div className="flex items-center gap-2">
            <span className="font-semibold text-orca-text">OrcaSlicer Plugins</span>
            {!native ? <Chip tone="warning">dev fixtures</Chip> : null}
          </div>
          <nav className="flex gap-1 ml-auto">
            {navLinks(activeNav).map((link) => (
              <a
                key={link.href}
                href={link.href}
                className={[
                  'px-3 py-1.5 rounded-md text-sm transition-colors',
                  link.active
                    ? 'bg-orca-accent/15 text-orca-accent'
                    : 'text-orca-muted hover:text-orca-text hover:bg-orca-bg',
                ].join(' ')}
              >
                {link.label}
              </a>
            ))}
          </nav>
        </div>
      </header>

      <main className="flex-1 max-w-5xl w-full mx-auto px-4 py-5">
        {route.kind === 'catalog' ? <CatalogPage /> : null}
        {route.kind === 'installed' ? <InstalledPage /> : null}
        {route.kind === 'detail' ? <PluginDetailPage id={route.id} /> : null}
        {route.kind === 'unknown' ? (
          <div className="text-sm text-orca-muted">
            Unknown route: <code>#{route.hash}</code>.{' '}
            <a href={hrefFor({ kind: 'catalog' })} className="underline">
              Return to catalog
            </a>
            .
          </div>
        ) : null}
      </main>

      <footer className="border-t border-orca-border bg-orca-panel">
        <div className="max-w-5xl mx-auto px-4 py-2 text-[11px] text-orca-muted">
          OrcaSlicer marketplace UI
        </div>
      </footer>
    </div>
  );
}
