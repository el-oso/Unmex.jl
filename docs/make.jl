# The host libmx isn't needed to render docs: Documenter only reads docstrings, and
# Unmex.__init__ just warns (does not fail) when the host .so is absent. Building it here
# would require LibMx as a direct docs dep (the host source now lives in LibMx), so skip it.
using Documenter, DocumenterVitepress, Unmex

makedocs(;
    modules = [Unmex],
    sitename = "Unmex.jl",
    authors = "el_oso",
    format = DocumenterVitepress.MarkdownVitepress(;
        repo = "github.com/el-oso/Unmex.jl",
        devbranch = "master",
        devurl = "dev",
        sidebar_drawer = true,
    ),
    pages = [
        "Home" => "index.md",
        "Guide" => [
            "guide/quickstart.md",
            "guide/how-it-works.md",
        ],
        "Reference" => [
            "reference/api.md",
        ],
    ],
    checkdocs = :exports,
    warnonly = [:missing_docs],
    remotes = nothing,
    doctest = false,
)

DocumenterVitepress.deploydocs(;
    repo = "github.com/el-oso/Unmex.jl",
    devbranch = "master",
    push_preview = true,
)
