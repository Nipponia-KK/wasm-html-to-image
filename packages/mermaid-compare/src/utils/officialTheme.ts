export type OfficialTheme = "default" | "dark" | "forest" | "neutral";

export interface OfficialThemeChoice {
  name: OfficialTheme;
  /** false: the official package has no theme of this name, `name` is only the closest stand-in */
  exact: boolean;
}

export function officialThemeFor(theme: string): OfficialThemeChoice {
  switch (theme) {
    case "light":
    case "default":
    case "base":
      return { name: "default", exact: true };
    case "dark":
      return { name: "dark", exact: true };
    case "forest":
      return { name: "forest", exact: true };
    case "neutral":
      return { name: "neutral", exact: true };
    case "nord":
    case "dracula":
    case "solarized-dark":
    case "rose-pine":
      return { name: "dark", exact: false };
    case "sepia":
      return { name: "neutral", exact: false };
    default:
      return { name: "default", exact: false };
  }
}
