-- washi (和紙) for Neovim: navy ink on sakura paper, light blue and pink, a rose seal.
-- The light half of the washi rice; sumi.lua is the same sheet soaked in ink.
--
-- Plugin-free. Copy this file to ~/.config/nvim/colors/ (or put the editors/nvim directory on
-- 'runtimepath') and run `:colorscheme washi`. True colour with 'termguicolors' (Neovim's default
-- in kitty); without it, the ANSI indices match the washi kitty palette. `:set background=dark`
-- turns the page to sumi.
--
-- Print, not neon: ink text, comments pencilled in italic, keywords in orchid, strings in mint,
-- functions in blue, types in teal, numbers in honey, the preprocessor and the accents in
-- sakura, and raspberry only for the loud things (errors, TODO seals, escapes). The palette
-- keeps sumi's names (caramel, bean, ...), as the code below it is shared with sumi.lua.

-- Each colour is { truecolor, ANSI index }. An index of nil leaves the terminal's own colour;
-- 255 is the xterm light grey, for the raised surfaces the 16 colours have nothing close to.
local p = {
  -- The sheet
  bg          = { "#FBF9FB" },      -- paper (kitty's background)
  fold        = { "#F3EFF5" },      -- cursor line: a faint fold in the paper
  sunken      = { "#EEE9F0" },      -- closed folds
  raised      = { "#F1ECF2", 255 }, -- floats and menus
  bar         = { "#EEE9F0", 255 }, -- inactive tabs, menu scrollbar
  sel         = { "#DCEAF8" },      -- selection, sky-tinted
  ref         = { "#EAEFF7" },      -- other uses of the word under the cursor
  match       = { "#F3D6E2" },      -- matching bracket
  search      = { "#F7D3E1", 13 },  -- search hits, a sakura highlighter stroke

  -- Pencil and ink
  line        = { "#DAD3E0", 7 },   -- hairlines: separators, whitespace
  faint       = { "#9EA2B8", 7 },   -- line numbers
  muted       = { "#6B708C", 7 },   -- comments
  pencil      = { "#5A607C", 8 },   -- fields, unhighlighted code blocks
  fg          = { "#363C58", 0 },   -- navy ink
  ui_fg       = { "#363C58", 0 },   -- text on floats and menus
  search_fg   = { "#363C58", 0 },   -- text on search hits
  paper       = { "#FBF9FB", 15 },  -- text on ink, sakura and the seal

  -- Chrome
  status      = { "#363C58", 0 },   -- the current window's status line, inked in
  status_fg   = { "#FBF9FB", 15 },
  tab_sel     = { "#363C58", 0 },   -- the current tab, inked in like kitty's
  tab_sel_fg  = { "#FBF9FB", 15 },
  mark        = { "#363C58", 0 },   -- the selected menu item, inked in
  mark_accent = { "#F2A0BE", 13 },  -- its matched letters, in sakura
  outline     = { "#4A5170", 0 },   -- float borders: the 2px ink outline
  shadow      = { "#A9C9EC", 12 },  -- border = "shadow": a hard offset printed in sky blue
  cursor      = { "#363C58", 0 },

  -- Colour
  caramel     = { "#B8467A", 13 },  -- the accent as text on paper: sakura, deep enough to read
  bean        = { "#B8467A", 13 },  -- the accent as a fill: the same sakura, under paper text
  seal        = { "#B83B63", 1 },   -- the rose hanko, deepened to raspberry for text
  green       = { "#3F7D52", 2 },   -- mint
  ochre       = { "#C47F00", 3 },   -- honey
  indigo      = { "#3570B3", 4 },   -- blue
  plum        = { "#A2488C", 5 },   -- orchid
  teal        = { "#23788A", 6 },

  -- Washes of colour behind diagnostics and diffs
  seal_bg     = { "#F9E1EA" },
  green_bg    = { "#E1EFE5" },
  ochre_bg    = { "#FBF0D9" },
  ochre_bg2   = { "#F5DFA8" },      -- changed text within a changed line
  indigo_bg   = { "#E3ECF8" },
  teal_bg     = { "#DFEFF1" },

  shadow_blend = 0, -- how much the shadow lets through: none
}

local ansi = {
  "#363C58", "#B83B63", "#3F7D52", "#C47F00", "#3570B3", "#A2488C", "#23788A", "#6B708C",
  "#5A607C", "#CE5A82", "#529568", "#D4911C", "#4E8BCB", "#B8619F", "#3592A3", "#FBF9FB",
}

-- `:set background=dark` while washi is up reloads it: turn the page to sumi instead (once the
-- reload is over, as Neovim does not nest colour schemes).
if vim.g.colors_name == "washi" and vim.o.background == "dark" then
  vim.schedule(function() vim.cmd.colorscheme("sumi") end)
  return
end

vim.cmd("highlight clear")
if vim.fn.exists("syntax_on") == 1 then vim.cmd("syntax reset") end
vim.o.background = "light"
vim.g.colors_name = "washi"

-- Everything below is shared verbatim with sumi.lua; only the palette above differs.

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
