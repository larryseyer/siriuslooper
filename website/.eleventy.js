module.exports = function (eleventyConfig) {
  eleventyConfig.addPassthroughCopy({ "src/assets": "assets" });
  eleventyConfig.addPassthroughCopy({ "src/CNAME": "CNAME" });
  eleventyConfig.addPassthroughCopy({ "src/robots.txt": "robots.txt" });

  eleventyConfig.addCollection("docs", (api) =>
    api.getFilteredByGlob("src/docs/*.md").sort((a, b) =>
      (a.data.order || 0) - (b.data.order || 0)
    )
  );

  eleventyConfig.addFilter("slug", (str) =>
    String(str || "")
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-|-$/g, "")
  );

  eleventyConfig.addFilter("absoluteUrl", (path, base) => {
    try { return new URL(path, base).toString(); }
    catch { return path; }
  });

  eleventyConfig.addFilter("urlEncode", (str) =>
    encodeURIComponent(String(str || ""))
  );

  eleventyConfig.addFilter("isoDate", (d) => new Date(d).toISOString());

  // Long-form docs use a `# Part X` convention internally. Demote any
  // remaining `<h1>` in markdown content to `<h2>` so the layout's
  // single `<h1>` remains the page's only top-level heading.
  eleventyConfig.amendLibrary("md", (mdLib) => {
    mdLib.core.ruler.push("demote-h1", (state) => {
      state.tokens.forEach((t) => {
        if ((t.type === "heading_open" || t.type === "heading_close") && t.tag === "h1") {
          t.tag = "h2";
          if (t.markup) t.markup = "##";
        }
      });
    });
  });

  return {
    dir: { input: "src", includes: "_includes", data: "_data", output: "_site" },
    markdownTemplateEngine: "njk",
    htmlTemplateEngine: "njk",
    templateFormats: ["njk", "md", "html"],
  };
};
