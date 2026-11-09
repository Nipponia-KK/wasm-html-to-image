/**
 * Normalizes SVG markup from both wasm-html-to-image and official mermaid
 * so that both diagrams scale properly within their containers without overflowing
 * or shrinking excessively.
 */
export function normalizeSvg(svg: string): string {
  if (!svg || typeof svg !== "string") return svg;

  return svg.replace(/<svg\b([^>]*?)>/i, (_match, attrs) => {
    let newAttrs = attrs;

    // 1. Extract width and height to construct viewBox if viewBox is missing
    const widthMatch = newAttrs.match(/\bwidth="([\d.]+)(?:px)?"/i);
    const heightMatch = newAttrs.match(/\bheight="([\d.]+)(?:px)?"/i);
    const hasViewBox = /\bviewBox=/i.test(newAttrs);

    let vb = "";
    if (!hasViewBox && widthMatch && heightMatch) {
      vb = `viewBox="0 0 ${widthMatch[1]} ${heightMatch[1]}"`;
    }

    // 2. Remove restrictive inline max-width (official mermaid adds max-width: XXXpx)
    newAttrs = newAttrs.replace(/\bstyle="([^"]*)"/i, (_: string, styleContent: string) => {
      const cleaned = styleContent
        .replace(/max-width:\s*[^;]+;?/gi, "")
        .replace(/width:\s*[^;]+;?/gi, "")
        .trim();
      return cleaned ? `style="${cleaned}"` : "";
    });

    // 3. Normalize width & height to 100% so SVG respects container sizing
    if (/\bwidth=/i.test(newAttrs)) {
      newAttrs = newAttrs.replace(/\bwidth="[^"]*"/i, 'width="100%"');
    } else {
      newAttrs += ' width="100%"';
    }

    if (/\bheight=/i.test(newAttrs)) {
      newAttrs = newAttrs.replace(/\bheight="[^"]*"/i, 'height="100%"');
    } else {
      newAttrs += ' height="100%"';
    }

    // 4. Ensure preserveAspectRatio is set to meet
    if (!/\bpreserveAspectRatio=/i.test(newAttrs)) {
      newAttrs += ' preserveAspectRatio="xMidYMid meet"';
    }

    if (vb) {
      newAttrs += ` ${vb}`;
    }

    return `<svg ${newAttrs}>`;
  });
}
