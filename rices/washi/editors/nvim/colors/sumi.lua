-- sumi (墨) for Neovim: the washi sheet soaked in ink under a lamp. Cream text on warm
-- brown-black, caramel lit brighter, the vermilion seal kept for the loud things.
-- The dark half of the washi rice; washi.lua is the paper by day.
--
-- Plugin-free. Copy this file to ~/.config/nvim/colors/ (or put the editors/nvim directory on
-- 'runtimepath') and run `:colorscheme sumi`. True colour with 'termguicolors' (Neovim's default
-- in kitty); without it, the ANSI indices match the sumi kitty palette. `:set background=light`
-- turns the page to washi.
--
-- Print, not neon: cream text, comments pencilled in italic, keywords in plum, strings in moss,
-- functions in indigo, types in teal, numbers in ochre, the preprocessor and the accents in
-- caramel, and vermilion only for the loud things (errors, TODO seals, escapes).

-- Each colour is { truecolor, ANSI index }. An index of nil leaves the terminal's own colour.
local p = {
  -- The sheet
  bg          = { "#1A1613" },      -- soaked paper (kitty's background)
  fold        = { "#241E1A", 0 },   -- cursor line: a faint fold in the paper
  sunken      = { "#120F0D" },      -- closed folds, pressed into the sheet
  raised      = { "#26201C", 0 },   -- floats and menus
  bar         = { "#26201C", 0 },   -- inactive tabs, menu scrollbar
  sel         = { "#4A3A2E" },      -- selection, caramel-tinted
  ref         = { "#342920" },      -- other uses of the word under the cursor
  match       = { "#4E3D2E" },      -- matching bracket
  search      = { "#66502A", 3 },   -- search hits, a highlighter stroke

  -- Pencil and ink
  line        = { "#3A302A", 8 },   -- hairlines: separators, whitespace
  faint       = { "#6B5F55", 8 },   -- line numbers
  muted       = { "#A89A88", 8 },   -- comments
  pencil      = { "#CDBFAD", 7 },   -- fields, unhighlighted code blocks
  fg          = { "#E8DCCB", 15 },  -- cream
  ui_fg       = { "#E8DCCB", 15 },  -- text on floats and menus
  search_fg   = { "#E8DCCB", 0 },   -- text on search hits
  paper       = { "#1A1613", 0 },   -- text on caramel and the seal

  -- Chrome
  status      = { "#3A302A", 8 },   -- the current window's status line
  status_fg   = { "#E8DCCB", 15 },
  tab_sel     = { "#3A302A", 8 },   -- the current tab, its name lit in caramel
  tab_sel_fg  = { "#D6A06E", 11 },
  mark        = { "#D6A06E", 11 },  -- the selected menu item, lit like kitty's current tab
  mark_accent = { "#5C3518", 0 },   -- its matched letters
  outline     = { "#A86A3C", 3 },   -- float borders: caramel, lamp-lit
  shadow      = { "#000000", 0 },   -- border = "shadow": a soft dark wash
  cursor      = { "#E2AE62", 3 },

  -- Colour
  caramel     = { "#D6A06E", 11 },  -- the accent as text
  bean        = { "#D6A06E", 11 },  -- the accent as a fill (the paw's toe beans)
  seal        = { "#D96F5E", 1 },   -- vermilion hanko
  green       = { "#A3B47A", 2 },   -- moss
  ochre       = { "#E2AE62", 3 },
  indigo      = { "#8EA8BC", 4 },
  plum        = { "#C98EA4", 5 },
  teal        = { "#8DB8AA", 6 },

  -- Washes of colour behind diagnostics and diffs
  seal_bg     = { "#35221E" },
  green_bg    = { "#2D2C21" },
  ochre_bg    = { "#362B1E" },
  ochre_bg2   = { "#56442B" },      -- changed text within a changed line
  indigo_bg   = { "#2A2A2B" },
  teal_bg     = { "#2A2D28" },

  shadow_blend = 50, -- how much the shadow lets through, 0-100
}

local ansi = {
  "#221D1A", "#D96F5E", "#A3B47A", "#E2AE62", "#8EA8BC", "#C98EA4", "#8DB8AA", "#CDBFAD",
  "#6B5F55", "#EC8A78", "#BACB92", "#F2C784", "#A9C1D3", "#DCA7BA", "#A6D0C2", "#F5ECE0",
}

-- `:set background=light` while sumi is up reloads it: turn the page to washi instead (once the
-- reload is over, as Neovim does not nest colour schemes).
if vim.g.colors_name == "sumi" and vim.o.background == "light" then
  vim.schedule(function() vim.cmd.colorscheme("washi") end)
  return
end

vim.cmd("highlight clear")
if vim.fn.exists("syntax_on") == 1 then vim.cmd("syntax reset") end
vim.o.background = "dark"
vim.g.colors_name = "sumi"

-- Everything below is shared verbatim with washi.lua; only the palette above differs.

-- hl(group, spec): fg/bg/sp take palette colours and fill in both the truecolor and the ANSI
-- value; other keys (bold, link, cterm, ctermfg...) pass through to nvim_set_hl unchanged.
local function hl(group, spec)
  local o = {}
  for k, v in pairs(spec) do
    if k == "fg" or k == "bg" or k == "sp" then
      o[k] = v[1]
      if k ~= "sp" and v[2] and o["cterm" .. k] == nil then o["cterm" .. k] = v[2] end
    else
      o[k] = v
    end
  end
  vim.api.nvim_set_hl(0, group, o)
end

local groups = {
  -- Editor ------------------------------------------------------------------------------------
  Normal           = { fg = { p.fg[1] }, bg = { p.bg[1] } },
  NormalFloat      = { fg = p.ui_fg, bg = p.raised },
  FloatBorder      = { fg = p.outline, bg = p.raised },
  FloatTitle       = { fg = p.caramel, bg = p.raised, bold = true },
  FloatFooter      = { fg = p.muted, bg = p.raised },
  FloatShadow      = { bg = p.shadow, blend = p.shadow_blend },
  FloatShadowThrough = { bg = p.shadow, blend = 100 },
  Cursor           = { fg = p.paper, bg = p.cursor },
  lCursor          = { link = "Cursor" },
  CursorIM         = { link = "Cursor" },
  TermCursor       = { reverse = true },
  CursorLine       = { bg = p.fold },
  CursorColumn     = { bg = p.fold },
  ColorColumn      = { bg = p.fold },
  LineNr           = { fg = p.faint },
  CursorLineNr     = { fg = p.caramel, bg = p.fold, bold = true },
  CursorLineFold   = { fg = p.caramel, bg = p.fold },
  CursorLineSign   = { bg = p.fold },
  SignColumn       = { fg = p.faint },
  FoldColumn       = { fg = p.faint },
  Folded           = { fg = p.muted, bg = p.sunken, italic = true },
  WinSeparator     = { fg = p.line },
  VertSplit        = { link = "WinSeparator" },
  StatusLine       = { fg = p.status_fg, bg = p.status },
  StatusLineNC     = { fg = p.muted, bg = p.fold },
  StatusLineTerm   = { link = "StatusLine" },
  StatusLineTermNC = { link = "StatusLineNC" },
  WinBar           = { fg = p.fg, bold = true },
  WinBarNC         = { fg = p.muted },
  TabLine          = { fg = p.muted, bg = p.bar },
  TabLineSel       = { fg = p.tab_sel_fg, bg = p.tab_sel, bold = true },
  TabLineFill      = { bg = { p.bg[1] } },
  Pmenu            = { fg = p.ui_fg, bg = p.raised },
  PmenuSel         = { fg = p.paper, bg = p.mark },
  PmenuKind        = { fg = p.teal, bg = p.raised },
  PmenuKindSel     = { fg = p.paper, bg = p.mark },
  PmenuExtra       = { fg = p.muted, bg = p.raised },
  PmenuExtraSel    = { fg = p.paper, bg = p.mark },
  PmenuMatch       = { fg = p.caramel, bg = p.raised, bold = true },
  PmenuMatchSel    = { fg = p.mark_accent, bg = p.mark, bold = true },
  PmenuSbar        = { bg = p.bar },
  PmenuThumb       = { bg = p.faint },
  ComplMatchIns    = { fg = p.muted },
  WildMenu         = { link = "PmenuSel" },
  Visual           = { bg = p.sel, cterm = { reverse = true } },
  VisualNOS        = { link = "Visual" },
  VisualNC         = { link = "Visual" },
  Search           = { fg = p.search_fg, bg = p.search },
  CurSearch        = { fg = p.paper, bg = p.bean, bold = true },
  IncSearch        = { link = "CurSearch" },
  Substitute       = { fg = p.paper, bg = p.seal },
  MatchParen       = { bg = p.match, bold = true, cterm = { bold = true, underline = true } },
  QuickFixLine     = { bg = p.ref, bold = true },
  NonText          = { fg = p.line },
  EndOfBuffer      = { fg = p.line },
  Whitespace       = { fg = p.line },
  SpecialKey       = { fg = p.faint },
  Conceal          = { fg = p.muted },
  Directory        = { fg = p.indigo },
  Title            = { fg = p.caramel, bold = true },
  Question         = { fg = p.green },
  MoreMsg          = { fg = p.green },
  ModeMsg          = { fg = p.fg, bold = true },
  MsgArea          = {},
  MsgSeparator     = { fg = p.line, bg = p.bar },
  WarningMsg       = { fg = p.ochre },
  ErrorMsg         = { fg = p.seal, bold = true },
  SpellBad         = { undercurl = true, sp = p.seal },
  SpellCap         = { undercurl = true, sp = p.indigo },
  SpellLocal       = { undercurl = true, sp = p.teal },
  SpellRare        = { undercurl = true, sp = p.plum },
  DiffAdd          = { bg = p.green_bg, ctermfg = p.green[2] },
  DiffChange       = { bg = p.ochre_bg, ctermfg = p.ochre[2] },
  DiffDelete       = { fg = p.seal, bg = p.seal_bg },
  DiffText         = { bg = p.ochre_bg2, bold = true, ctermfg = p.ochre[2] },
  Added            = { fg = p.green },
  Changed          = { fg = p.ochre },
  Removed          = { fg = p.seal },
  SnippetTabstop   = { bg = p.ref },
  RedrawDebugNormal = { reverse = true },

  -- Diagnostics -------------------------------------------------------------------------------
  DiagnosticError            = { fg = p.seal },
  DiagnosticWarn             = { fg = p.ochre },
  DiagnosticInfo             = { fg = p.indigo },
  DiagnosticHint             = { fg = p.teal },
  DiagnosticOk               = { fg = p.green },
  DiagnosticVirtualTextError = { fg = p.seal, bg = p.seal_bg },
  DiagnosticVirtualTextWarn  = { fg = p.ochre, bg = p.ochre_bg },
  DiagnosticVirtualTextInfo  = { fg = p.indigo, bg = p.indigo_bg },
  DiagnosticVirtualTextHint  = { fg = p.teal, bg = p.teal_bg },
  DiagnosticVirtualTextOk    = { fg = p.green, bg = p.green_bg },
  DiagnosticVirtualLinesError = { fg = p.seal },
  DiagnosticVirtualLinesWarn = { fg = p.ochre },
  DiagnosticVirtualLinesInfo = { fg = p.indigo },
  DiagnosticVirtualLinesHint = { fg = p.teal },
  DiagnosticVirtualLinesOk   = { fg = p.green },
  DiagnosticUnderlineError   = { undercurl = true, sp = p.seal },
  DiagnosticUnderlineWarn    = { undercurl = true, sp = p.ochre },
  DiagnosticUnderlineInfo    = { undercurl = true, sp = p.indigo },
  DiagnosticUnderlineHint    = { undercurl = true, sp = p.teal },
  DiagnosticUnderlineOk      = { undercurl = true, sp = p.green },
  DiagnosticSignError        = { fg = p.seal },
  DiagnosticSignWarn         = { fg = p.ochre },
  DiagnosticSignInfo         = { fg = p.indigo },
  DiagnosticSignHint         = { fg = p.teal },
  DiagnosticSignOk           = { fg = p.green },
  DiagnosticFloatingError    = { fg = p.seal },
  DiagnosticFloatingWarn     = { fg = p.ochre },
  DiagnosticFloatingInfo     = { fg = p.indigo },
  DiagnosticFloatingHint     = { fg = p.teal },
  DiagnosticFloatingOk       = { fg = p.green },
  DiagnosticUnnecessary      = { fg = p.faint },
  DiagnosticDeprecated       = { strikethrough = true, sp = p.muted },

  -- LSP ---------------------------------------------------------------------------------------
  LspReferenceText            = { bg = p.ref },
  LspReferenceRead            = { bg = p.ref },
  LspReferenceWrite           = { bg = p.ref, underline = true, sp = p.caramel },
  LspReferenceTarget          = { link = "LspReferenceText" },
  LspInlayHint                = { fg = p.faint, italic = true },
  LspCodeLens                 = { fg = p.faint, italic = true },
  LspCodeLensSeparator        = { fg = p.line },
  LspSignatureActiveParameter = { fg = p.caramel, bold = true, underline = true },

  -- Syntax (legacy groups, still used by every non-Tree-sitter filetype) ------------------------
  Comment        = { fg = p.muted, italic = true },
  Constant       = { fg = p.ochre },
  String         = { fg = p.green },
  Character      = { fg = p.green },
  Number         = { fg = p.ochre },
  Boolean        = { fg = p.ochre },
  Float          = { fg = p.ochre },
  Identifier     = { fg = p.fg },
  Function       = { fg = p.indigo },
  Statement      = { fg = p.plum },
  Conditional    = { fg = p.plum },
  Repeat         = { fg = p.plum },
  Label          = { fg = p.caramel },
  Operator       = { fg = p.caramel },
  Keyword        = { fg = p.plum },
  Exception      = { fg = p.seal },
  PreProc        = { fg = p.caramel },
  Include        = { fg = p.caramel },
  Define         = { fg = p.caramel },
  Macro          = { fg = p.caramel },
  PreCondit      = { fg = p.caramel },
  Type           = { fg = p.teal },
  StorageClass   = { fg = p.plum },
  Structure      = { fg = p.plum },
  Typedef        = { fg = p.plum },
  Special        = { fg = p.caramel },
  SpecialChar    = { fg = p.seal },
  Tag            = { fg = p.plum },
  Delimiter      = { fg = p.muted },
  SpecialComment = { fg = p.muted, bold = true, italic = true },
  Debug          = { fg = p.seal },
  Underlined     = { fg = p.indigo, underline = true },
  Ignore         = { fg = p.faint },
  Error          = { fg = p.seal, bold = true },
  Todo           = { fg = p.paper, bg = p.seal, bold = true },

  -- Tree-sitter -------------------------------------------------------------------------------
  ["@variable"]                   = { fg = p.fg },
  ["@variable.builtin"]           = { fg = p.plum, italic = true },
  ["@variable.parameter"]         = { fg = p.fg },
  ["@variable.parameter.builtin"] = { fg = p.plum, italic = true },
  ["@variable.member"]            = { fg = p.pencil },
  ["@property"]                   = { fg = p.pencil },

  ["@constant"]         = { fg = p.ochre },
  ["@constant.builtin"] = { fg = p.ochre },
  ["@constant.macro"]   = { fg = p.ochre },

  ["@module"]         = { fg = p.teal },
  ["@module.builtin"] = { fg = p.teal, italic = true },
  ["@label"]          = { fg = p.caramel },

  ["@string"]               = { fg = p.green },
  ["@string.documentation"] = { fg = p.green, italic = true },
  ["@string.regexp"]        = { fg = p.teal },
  ["@string.escape"]        = { fg = p.seal },
  ["@string.special"]       = { fg = p.caramel },
  ["@string.special.symbol"] = { fg = p.ochre },
  ["@string.special.path"]  = { fg = p.green, underline = true },
  ["@string.special.url"]   = { fg = p.indigo, underline = true },
  ["@character"]            = { fg = p.green },
  ["@character.special"]    = { fg = p.caramel },

  ["@boolean"]      = { fg = p.ochre },
  ["@number"]       = { fg = p.ochre },
  ["@number.float"] = { fg = p.ochre },

  ["@type"]            = { fg = p.teal },
  ["@type.builtin"]    = { fg = p.teal },
  ["@type.definition"] = { fg = p.teal },

  ["@attribute"]         = { fg = p.caramel },
  ["@attribute.builtin"] = { fg = p.caramel },

  ["@function"]             = { fg = p.indigo },
  ["@function.builtin"]     = { fg = p.indigo },
  ["@function.call"]        = { fg = p.indigo },
  ["@function.macro"]       = { fg = p.caramel },
  ["@function.method"]      = { fg = p.indigo },
  ["@function.method.call"] = { fg = p.indigo },
  ["@constructor"]          = { fg = p.teal },
  ["@constructor.lua"]      = { link = "@punctuation.bracket" }, -- a table's braces
  ["@operator"]             = { fg = p.caramel },

  ["@keyword"]                     = { fg = p.plum },
  ["@keyword.coroutine"]           = { fg = p.plum },
  ["@keyword.function"]            = { fg = p.plum },
  ["@keyword.operator"]            = { fg = p.plum },
  ["@keyword.import"]              = { fg = p.caramel },
  ["@keyword.type"]                = { fg = p.plum },
  ["@keyword.modifier"]            = { fg = p.plum },
  ["@keyword.repeat"]              = { fg = p.plum },
  ["@keyword.return"]              = { fg = p.plum },
  ["@keyword.debug"]               = { fg = p.seal },
  ["@keyword.exception"]           = { fg = p.seal },
  ["@keyword.conditional"]         = { fg = p.plum },
  ["@keyword.conditional.ternary"] = { fg = p.caramel },
  ["@keyword.directive"]           = { fg = p.caramel },
  ["@keyword.directive.define"]    = { fg = p.caramel },

  ["@punctuation.delimiter"] = { fg = p.muted },
  ["@punctuation.bracket"]   = { fg = p.muted },
  ["@punctuation.special"]   = { fg = p.caramel },

  ["@comment"]               = { link = "Comment" },
  ["@comment.documentation"] = { fg = p.muted, italic = true },
  ["@comment.error"]         = { fg = p.paper, bg = p.seal, bold = true },
  ["@comment.warning"]       = { fg = p.paper, bg = p.ochre, bold = true },
  ["@comment.todo"]          = { fg = p.paper, bg = p.seal, bold = true },
  ["@comment.note"]          = { fg = p.paper, bg = p.indigo, bold = true },

  ["@markup.strong"]           = { bold = true },
  ["@markup.italic"]           = { italic = true },
  ["@markup.strikethrough"]    = { strikethrough = true },
  ["@markup.underline"]        = { underline = true },
  ["@markup.heading"]          = { fg = p.fg, bold = true },
  ["@markup.heading.1"]        = { fg = p.seal, bold = true },
  ["@markup.heading.2"]        = { fg = p.caramel, bold = true },
  ["@markup.heading.3"]        = { fg = p.indigo, bold = true },
  ["@markup.heading.4"]        = { fg = p.teal, bold = true },
  ["@markup.heading.5"]        = { fg = p.plum, bold = true },
  ["@markup.heading.6"]        = { fg = p.ochre, bold = true },
  ["@markup.quote"]            = { fg = p.muted, italic = true },
  ["@markup.math"]             = { fg = p.teal },
  ["@markup.link"]             = { fg = p.indigo },
  ["@markup.link.label"]       = { fg = p.indigo },
  ["@markup.link.url"]         = { fg = p.muted, underline = true },
  ["@markup.raw"]              = { fg = p.teal },
  ["@markup.raw.block"]        = { fg = p.pencil },
  ["@markup.list"]             = { fg = p.caramel },
  ["@markup.list.checked"]     = { fg = p.green },
  ["@markup.list.unchecked"]   = { fg = p.muted },
  ["@markup.environment"]      = { fg = p.plum },

  ["@diff.plus"]  = { link = "Added" },
  ["@diff.minus"] = { link = "Removed" },
  ["@diff.delta"] = { link = "Changed" },

  ["@tag"]           = { fg = p.plum },
  ["@tag.builtin"]   = { fg = p.plum },
  ["@tag.attribute"] = { fg = p.caramel },
  ["@tag.delimiter"] = { fg = p.muted },

  -- LSP semantic tokens: follow the Tree-sitter captures, so a language server only refines.
  -- @lsp.type.variable stays empty so Tree-sitter's builtins and parameters keep their colour.
  ["@lsp.type.class"]         = { link = "@type" },
  ["@lsp.type.comment"]       = { link = "@comment" },
  ["@lsp.type.decorator"]     = { link = "@attribute" },
  ["@lsp.type.enum"]          = { link = "@type" },
  ["@lsp.type.enumMember"]    = { link = "@constant" },
  ["@lsp.type.event"]         = { link = "@type" },
  ["@lsp.type.function"]      = { link = "@function" },
  ["@lsp.type.interface"]     = { link = "@type" },
  ["@lsp.type.keyword"]       = { link = "@keyword" },
  ["@lsp.type.macro"]         = { link = "@constant.macro" },
  ["@lsp.type.method"]        = { link = "@function.method" },
  ["@lsp.type.modifier"]      = { link = "@keyword.modifier" },
  ["@lsp.type.namespace"]     = { link = "@module" },
  ["@lsp.type.number"]        = { link = "@number" },
  ["@lsp.type.operator"]      = { link = "@operator" },
  ["@lsp.type.parameter"]     = { link = "@variable.parameter" },
  ["@lsp.type.property"]      = { link = "@property" },
  ["@lsp.type.regexp"]        = { link = "@string.regexp" },
  ["@lsp.type.string"]        = { link = "@string" },
  ["@lsp.type.struct"]        = { link = "@type" },
  ["@lsp.type.type"]          = { link = "@type" },
  ["@lsp.type.typeParameter"] = { link = "@type.definition" },
  ["@lsp.type.variable"]      = {},
  ["@lsp.mod.deprecated"]     = { strikethrough = true },
  ["@lsp.typemod.function.defaultLibrary"] = { link = "@function.builtin" },
  ["@lsp.typemod.method.defaultLibrary"]   = { link = "@function.builtin" },
  ["@lsp.typemod.variable.defaultLibrary"] = { link = "@variable.builtin" },
}

for group, spec in pairs(groups) do hl(group, spec) end

for i, c in ipairs(ansi) do vim.g["terminal_color_" .. (i - 1)] = c end
