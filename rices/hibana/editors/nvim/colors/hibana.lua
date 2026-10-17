-- hibana (火花) for Neovim: sparks on ash. Paper-white text on a grey storm, ember orange for
-- the keywords, the cursor and whatever has focus, a little blood red. The editor paints no
-- background of its own, so kitty's translucency (and paw's blur behind it) shows through the
-- text. hibana's palette is its own; the scheme follows the structure of paw's mochi scheme and
-- is paw's, GPL-3.0-or-later.
--
-- Plugin-free. Copy this file to ~/.config/nvim/colors/ (or put the editors/nvim directory on
-- 'runtimepath') and run `:colorscheme hibana`. True colour with 'termguicolors' (Neovim's
-- default in kitty); without it, the ANSI indices match the hibana kitty palette. In a terminal
-- without translucency the page is the terminal's own background: set it to hibana's base,
-- #161518.
--
-- The large backgrounds it does paint are these, and hibana's kitty theme lists every one of
-- them in transparent_background_colors, so they stay glass too instead of turning into slabs:
--   #111013  mantle: floats, menus, the status lines and the tab line
--   #232126  the cursor line and the colour column, a shade above surface0
--   #2A272E  surface1: closed folds, the menu's scrollbar track and snippet tabstops
--   #36323A  surface2: the selected menu item, the current tab, matching brackets, other uses
--            of the word under the cursor and the current quickfix line
--   #3D2B26  the selection, a smoky ember wash under text that keeps its colours
--   #2D2220  Helix's other selections (hibana.toml); Neovim has only the one
-- Everything else it paints is a small solid mark: the cursor, the current search hit, TODO
-- tags, the menu's scrollbar thumb, the shadow under floats that ask for one, and the washes of
-- colour behind search hits, diffs and diagnostics.
--
-- hibana's style: keywords in ember, functions in paper white and bold (their definitions in
-- flare), types in brass, strings in steel, numbers and constants in flare, builtins in cinder,
-- parameters in mauve, variables in subtext, operators and punctuation in overlay, comments in
-- overlay italic; the cursor and the focus in ember, links and info in rain.

-- Each colour is { truecolor, ANSI index }. An index of nil leaves the terminal's own colour.
local p = {
  -- The grounds, ink to paper, with a faint cool mauve tint
  crust     = { "#0B0A0D", 0 },
  mantle    = { "#111013", 0 },
  base      = { "#161518", 0 },
  surface0  = { "#1F1D22", 0 },
  surface1  = { "#2A272E", 0 },
  surface2  = { "#36323A", 8 },
  overlay0  = { "#4B4650", 8 },
  overlay1  = { "#6E6A72", 8 },
  overlay2  = { "#8F8A94", 7 },
  subtext0  = { "#A8A3AB", 7 },
  subtext1  = { "#C6C1C6", 7 },
  text      = { "#E2DDD9", 15 },

  -- The accents
  ember     = { "#FF7A33", 3 },   -- the accent: keywords, the cursor, focus
  flare     = { "#FFB070", 11 },  -- the white-hot core of a spark: numbers and constants
  blood     = { "#C8443C", 1 },   -- the deep red, a rare mark
  scarlet   = { "#E5594D", 1 },   -- the readable red: errors and deletions
  cinder    = { "#E0705C", 9 },   -- builtins and special variables, between the reds and ember
  brass     = { "#DCB36E", 11 },  -- types and warnings
  sage      = { "#A3B58F", 2 },   -- additions, regular expressions
  steel     = { "#8FB0B4", 6 },   -- strings
  rain      = { "#9FB3CC", 4 },   -- links, info, modules
  mauve     = { "#B39AAE", 5 },   -- parameters and labels

  -- Blends of them: the cursor line, the selection, and washes of colour behind search hits,
  -- diffs and diagnostics (each the accent over base). The washes behind search hits and diffs
  -- are stronger than an opaque page would need, so they still read where light wallpaper
  -- brightens the glass around them.
  ash       = { "#605C64", 8 },   -- inlay hints: between overlay0 and overlay1, under comments
  line      = { "#232126" },      -- the cursor line: a shade above surface0, so it reads on glass
  sel       = { "#3D2B26", 0 },   -- the selection: a smoky ember wash, as in kitty
  search    = { "#653721", 3 },   -- search hits: ember at 34% over base
  search_fg = { "#E2DDD9", 0 },   -- text on search hits
  add_bg    = { "#383B35" },      -- sage 24%
  del_bg    = { "#482525" },      -- scarlet 24%
  change_bg = { "#322B24" },      -- brass 14%
  text_bg   = { "#514432" },      -- changed text in a changed line: brass 30%
  scarlet_bg = { "#2B1C1D" },     -- scarlet 10%, behind virtual text
  brass_bg  = { "#2A2521" },
  rain_bg   = { "#24252A" },
  hint_bg   = { "#222124" },
  sage_bg   = { "#242524" },

  shadow_blend = 60, -- how much a float's shadow lets through, 0-100
}

local ansi = {
  "#2A272E", "#D9564A", "#A3B58F", "#F0A65A", "#8FA9C4", "#B39AAE", "#8FB0B4", "#C6C1C6",
  "#4B4650", "#EF6E60", "#B9CBA3", "#FFBF80", "#A9C1D9", "#CBB2C6", "#A8C9CC", "#E2DDD9",
}

vim.cmd("highlight clear")
if vim.fn.exists("syntax_on") == 1 then vim.cmd("syntax reset") end
vim.o.background = "dark"
vim.g.colors_name = "hibana"

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
  -- Editor: no background, so the glass shows ------------------------------------------------
  Normal           = { fg = p.text },
  NormalNC         = { fg = p.text },
  NormalFloat      = { fg = p.text, bg = p.mantle },
  FloatBorder      = { fg = p.overlay0, bg = p.mantle },
  FloatTitle       = { fg = p.ember, bg = p.mantle, bold = true },
  FloatFooter      = { fg = p.overlay1, bg = p.mantle },
  FloatShadow      = { bg = p.crust, blend = p.shadow_blend },
  FloatShadowThrough = { bg = p.crust, blend = 100 },
  Cursor           = { fg = p.crust, bg = p.ember },
  lCursor          = { link = "Cursor" },
  CursorIM         = { link = "Cursor" },
  TermCursor       = { fg = p.crust, bg = p.ember },
  CursorLine       = { bg = p.line },
  CursorColumn     = { bg = p.line },
  ColorColumn      = { bg = p.line },
  LineNr           = { fg = p.overlay0 },
  LineNrAbove      = { link = "LineNr" },
  LineNrBelow      = { link = "LineNr" },
  CursorLineNr     = { fg = p.ember, bold = true },
  CursorLineFold   = { fg = p.ember },
  CursorLineSign   = {},
  SignColumn       = { fg = p.surface2 },
  FoldColumn       = { fg = p.overlay0 },
  Folded           = { fg = p.subtext0, bg = p.surface1, italic = true },
  WinSeparator     = { fg = p.surface2 },
  VertSplit        = { link = "WinSeparator" },
  StatusLine       = { fg = p.text, bg = p.mantle },
  StatusLineNC     = { fg = p.overlay1, bg = p.mantle },
  StatusLineTerm   = { link = "StatusLine" },
  StatusLineTermNC = { link = "StatusLineNC" },
  WinBar           = { fg = p.text, bold = true },
  WinBarNC         = { fg = p.overlay1 },
  TabLine          = { fg = p.overlay1, bg = p.mantle },
  TabLineSel       = { fg = p.ember, bg = p.surface2, bold = true },
  TabLineFill      = {},
  Pmenu            = { fg = p.subtext0, bg = p.mantle },
  PmenuSel         = { fg = p.text, bg = p.surface2, bold = true },
  PmenuKind        = { fg = p.rain, bg = p.mantle },
  PmenuKindSel     = { fg = p.rain, bg = p.surface2, bold = true },
  PmenuExtra       = { fg = p.overlay1, bg = p.mantle },
  PmenuExtraSel    = { fg = p.overlay2, bg = p.surface2 },
  PmenuMatch       = { fg = p.ember, bg = p.mantle, bold = true },
  PmenuMatchSel    = { fg = p.ember, bg = p.surface2, bold = true },
  PmenuSbar        = { bg = p.surface1 },
  PmenuThumb       = { bg = p.overlay0 },
  ComplMatchIns    = { fg = p.overlay1 },
  WildMenu         = { link = "PmenuSel" },
  Visual           = { bg = p.sel, bold = true, cterm = { reverse = true } },
  VisualNOS        = { link = "Visual" },
  VisualNC         = { link = "Visual" },
  Search           = { fg = p.search_fg, bg = p.search },
  CurSearch        = { fg = p.crust, bg = p.ember, bold = true },
  IncSearch        = { link = "CurSearch" },
  Substitute       = { fg = p.crust, bg = p.scarlet },
  MatchParen       = { fg = p.flare, bg = p.surface2, bold = true, cterm = { bold = true, underline = true } },
  QuickFixLine     = { bg = p.surface2, bold = true },
  NonText          = { fg = p.overlay0 },
  EndOfBuffer      = { fg = p.surface1 },
  Whitespace       = { fg = p.surface2 },
  SpecialKey       = { fg = p.overlay0 },
  Conceal          = { fg = p.overlay1 },
  Directory        = { fg = p.rain },
  Title            = { fg = p.ember, bold = true },
  Question         = { fg = p.rain },
  MoreMsg          = { fg = p.rain },
  ModeMsg          = { fg = p.text, bold = true },
  MsgArea          = {},
  MsgSeparator     = { fg = p.surface2 },
  OkMsg            = { fg = p.sage },
  WarningMsg       = { fg = p.brass },
  ErrorMsg         = { fg = p.scarlet, bold = true, italic = true },
  SpellBad         = { undercurl = true, sp = p.scarlet },
  SpellCap         = { undercurl = true, sp = p.brass },
  SpellLocal       = { undercurl = true, sp = p.rain },
  SpellRare        = { undercurl = true, sp = p.mauve },
  DiffAdd          = { bg = p.add_bg, ctermfg = p.sage[2] },
  DiffChange       = { bg = p.change_bg, ctermfg = p.brass[2] },
  DiffDelete       = { fg = p.scarlet, bg = p.del_bg },
  DiffText         = { bg = p.text_bg, ctermfg = p.brass[2] },
  Added            = { fg = p.sage },
  Changed          = { fg = p.brass },
  Removed          = { fg = p.scarlet },
  SnippetTabstop   = { bg = p.surface1 },
  RedrawDebugNormal = { reverse = true },

  -- Diagnostics -------------------------------------------------------------------------------
  DiagnosticError            = { fg = p.scarlet },
  DiagnosticWarn             = { fg = p.brass },
  DiagnosticInfo             = { fg = p.rain },
  DiagnosticHint             = { fg = p.overlay2 },
  DiagnosticOk               = { fg = p.sage },
  DiagnosticVirtualTextError = { fg = p.scarlet, bg = p.scarlet_bg, italic = true },
  DiagnosticVirtualTextWarn  = { fg = p.brass, bg = p.brass_bg, italic = true },
  DiagnosticVirtualTextInfo  = { fg = p.rain, bg = p.rain_bg, italic = true },
  DiagnosticVirtualTextHint  = { fg = p.overlay2, bg = p.hint_bg, italic = true },
  DiagnosticVirtualTextOk    = { fg = p.sage, bg = p.sage_bg, italic = true },
  DiagnosticVirtualLinesError = { fg = p.scarlet },
  DiagnosticVirtualLinesWarn = { fg = p.brass },
  DiagnosticVirtualLinesInfo = { fg = p.rain },
  DiagnosticVirtualLinesHint = { fg = p.overlay2 },
  DiagnosticVirtualLinesOk   = { fg = p.sage },
  DiagnosticUnderlineError   = { undercurl = true, sp = p.scarlet },
  DiagnosticUnderlineWarn    = { undercurl = true, sp = p.brass },
  DiagnosticUnderlineInfo    = { undercurl = true, sp = p.rain },
  DiagnosticUnderlineHint    = { undercurl = true, sp = p.overlay2 },
  DiagnosticUnderlineOk      = { undercurl = true, sp = p.sage },
  DiagnosticSignError        = { fg = p.scarlet },
  DiagnosticSignWarn         = { fg = p.brass },
  DiagnosticSignInfo         = { fg = p.rain },
  DiagnosticSignHint         = { fg = p.overlay2 },
  DiagnosticSignOk           = { fg = p.sage },
  DiagnosticFloatingError    = { fg = p.scarlet },
  DiagnosticFloatingWarn     = { fg = p.brass },
  DiagnosticFloatingInfo     = { fg = p.rain },
  DiagnosticFloatingHint     = { fg = p.overlay2 },
  DiagnosticFloatingOk       = { fg = p.sage },
  DiagnosticUnnecessary      = { fg = p.overlay1 },
  DiagnosticDeprecated       = { strikethrough = true, sp = p.overlay1 },

  -- LSP ---------------------------------------------------------------------------------------
  LspReferenceText            = { bg = p.surface2 },
  LspReferenceRead            = { bg = p.surface2 },
  LspReferenceWrite           = { bg = p.surface2, underline = true, sp = p.ember },
  LspReferenceTarget          = { link = "LspReferenceText" },
  LspInlayHint                = { fg = p.ash, italic = true },
  LspCodeLens                 = { fg = p.ash, italic = true },
  LspCodeLensSeparator        = { fg = p.surface2 },
  LspSignatureActiveParameter = { fg = p.flare, bold = true, underline = true },

  -- Syntax (legacy groups, still used by every non-Tree-sitter filetype) ------------------------
  Comment        = { fg = p.overlay1, italic = true },
  Constant       = { fg = p.flare },
  String         = { fg = p.steel },
  Character      = { fg = p.steel },
  Number         = { fg = p.flare },
  Boolean        = { fg = p.flare },
  Float          = { fg = p.flare },
  Identifier     = { fg = p.subtext1 },
  Function       = { fg = p.text, bold = true },
  Statement      = { fg = p.ember },
  Conditional    = { fg = p.ember },
  Repeat         = { fg = p.ember },
  Label          = { fg = p.mauve },
  Operator       = { fg = p.overlay2 },
  Keyword        = { fg = p.ember },
  Exception      = { fg = p.ember },
  PreProc        = { fg = p.cinder },
  Include        = { fg = p.ember },
  Define         = { fg = p.cinder },
  Macro          = { fg = p.cinder },
  PreCondit      = { fg = p.cinder },
  Type           = { fg = p.brass },
  StorageClass   = { fg = p.ember },
  Structure      = { fg = p.ember },
  Typedef        = { fg = p.ember },
  Special        = { fg = p.flare },
  SpecialChar    = { fg = p.flare },
  Tag            = { fg = p.ember },
  Delimiter      = { fg = p.overlay2 },
  SpecialComment = { fg = p.overlay2, bold = true, italic = true },
  Debug          = { fg = p.cinder },
  Underlined     = { fg = p.rain, underline = true },
  Ignore         = { fg = p.overlay0 },
  Error          = { fg = p.scarlet, bold = true },
  Todo           = { fg = p.crust, bg = p.ember, bold = true },

  -- Tree-sitter -------------------------------------------------------------------------------
  ["@variable"]                   = { fg = p.subtext1 },
  ["@variable.builtin"]           = { fg = p.cinder },
  ["@variable.parameter"]         = { fg = p.mauve },
  ["@variable.parameter.builtin"] = { fg = p.mauve, italic = true },
  ["@variable.member"]            = { fg = p.text },
  ["@property"]                   = { fg = p.text },

  ["@constant"]         = { fg = p.flare },
  ["@constant.builtin"] = { fg = p.flare },
  ["@constant.macro"]   = { fg = p.flare },

  ["@module"]         = { fg = p.rain, italic = true },
  ["@module.builtin"] = { fg = p.rain, italic = true },
  ["@label"]          = { fg = p.mauve },

  ["@string"]               = { fg = p.steel },
  ["@string.documentation"] = { fg = p.steel, italic = true },
  ["@string.regexp"]        = { fg = p.sage },
  ["@string.escape"]        = { fg = p.flare },
  ["@string.special"]       = { fg = p.flare },
  ["@string.special.symbol"] = { fg = p.flare },
  ["@string.special.path"]  = { fg = p.steel, underline = true },
  ["@string.special.url"]   = { fg = p.rain, underline = true },
  ["@character"]            = { fg = p.steel },
  ["@character.special"]    = { fg = p.flare },

  ["@boolean"]      = { fg = p.flare },
  ["@number"]       = { fg = p.flare },
  ["@number.float"] = { fg = p.flare },

  ["@type"]            = { fg = p.brass },
  ["@type.builtin"]    = { fg = p.brass, italic = true },
  ["@type.definition"] = { fg = p.brass },

  ["@attribute"]         = { fg = p.brass },
  ["@attribute.builtin"] = { fg = p.brass },

  -- A function's name glows in flare where it is defined, and stays paper white where it is
  -- called; both bold.
  ["@function"]             = { fg = p.flare, bold = true },
  ["@function.builtin"]     = { fg = p.cinder, bold = true },
  ["@function.call"]        = { fg = p.text, bold = true },
  ["@function.macro"]       = { fg = p.cinder },
  ["@function.method"]      = { fg = p.flare, bold = true },
  ["@function.method.call"] = { fg = p.text, bold = true },
  ["@constructor"]          = { fg = p.brass },
  ["@constructor.lua"]      = { link = "@punctuation.bracket" }, -- a table's braces
  ["@operator"]             = { fg = p.overlay2 },

  ["@keyword"]                     = { fg = p.ember },
  ["@keyword.coroutine"]           = { fg = p.ember },
  ["@keyword.function"]            = { fg = p.ember },
  ["@keyword.operator"]            = { fg = p.ember },
  ["@keyword.import"]              = { fg = p.ember },
  ["@keyword.type"]                = { fg = p.ember },
  ["@keyword.modifier"]            = { fg = p.ember },
  ["@keyword.repeat"]              = { fg = p.ember },
  ["@keyword.return"]              = { fg = p.ember },
  ["@keyword.debug"]               = { fg = p.cinder },
  ["@keyword.exception"]           = { fg = p.ember },
  ["@keyword.conditional"]         = { fg = p.ember },
  ["@keyword.conditional.ternary"] = { fg = p.ember },
  ["@keyword.directive"]           = { fg = p.cinder },
  ["@keyword.directive.define"]    = { fg = p.cinder },

  ["@punctuation.delimiter"] = { fg = p.overlay2 },
  ["@punctuation.bracket"]   = { fg = p.overlay2 },
  ["@punctuation.special"]   = { fg = p.flare },

  ["@comment"]               = { link = "Comment" },
  ["@comment.documentation"] = { link = "Comment" },
  ["@comment.error"]         = { fg = p.crust, bg = p.scarlet, bold = true },
  ["@comment.warning"]       = { fg = p.crust, bg = p.brass, bold = true },
  ["@comment.todo"]          = { fg = p.crust, bg = p.ember, bold = true },
  ["@comment.note"]          = { fg = p.crust, bg = p.rain, bold = true },

  ["@markup.strong"]           = { fg = p.flare, bold = true },
  ["@markup.italic"]           = { italic = true },
  ["@markup.strikethrough"]    = { fg = p.overlay2, strikethrough = true },
  ["@markup.underline"]        = { underline = true },
  ["@markup.heading"]          = { fg = p.ember, bold = true },
  ["@markup.heading.1"]        = { fg = p.ember, bold = true },
  ["@markup.heading.2"]        = { fg = p.flare, bold = true },
  ["@markup.heading.3"]        = { fg = p.brass, bold = true },
  ["@markup.heading.4"]        = { fg = p.sage, bold = true },
  ["@markup.heading.5"]        = { fg = p.steel, bold = true },
  ["@markup.heading.6"]        = { fg = p.rain, bold = true },
  ["@markup.quote"]            = { fg = p.subtext0, italic = true },
  ["@markup.math"]             = { fg = p.brass },
  ["@markup.link"]             = { fg = p.rain },
  ["@markup.link.label"]       = { fg = p.rain },
  ["@markup.link.url"]         = { fg = p.rain, italic = true, underline = true },
  ["@markup.raw"]              = { fg = p.steel },
  ["@markup.raw.block"]        = { fg = p.subtext1 },
  ["@markup.list"]             = { fg = p.ember },
  ["@markup.list.checked"]     = { fg = p.sage },
  ["@markup.list.unchecked"]   = { fg = p.overlay1 },
  ["@markup.environment"]      = { fg = p.ember },

  ["@diff.plus"]  = { link = "Added" },
  ["@diff.minus"] = { link = "Removed" },
  ["@diff.delta"] = { link = "Changed" },

  ["@tag"]           = { fg = p.ember },
  ["@tag.builtin"]   = { fg = p.ember },
  ["@tag.attribute"] = { fg = p.brass },
  ["@tag.delimiter"] = { fg = p.overlay2 },

  -- LSP semantic tokens: follow the Tree-sitter captures, so a language server only refines.
  -- @lsp.type.variable stays empty so Tree-sitter's builtins and parameters keep their colour.
  -- A server marks every use of a function the same way, so its functions and methods follow
  -- the calls, and only the ones it marks as a declaration or definition glow like @function.
  ["@lsp.type.class"]         = { link = "@type" },
  ["@lsp.type.comment"]       = { link = "@comment" },
  ["@lsp.type.decorator"]     = { link = "@attribute" },
  ["@lsp.type.enum"]          = { link = "@type" },
  ["@lsp.type.enumMember"]    = { link = "@constant" },
  ["@lsp.type.event"]         = { link = "@type" },
  ["@lsp.type.function"]      = { link = "@function.call" },
  ["@lsp.type.interface"]     = { link = "@type" },
  ["@lsp.type.keyword"]       = { link = "@keyword" },
  ["@lsp.type.macro"]         = { link = "@constant.macro" },
  ["@lsp.type.method"]        = { link = "@function.method.call" },
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
  ["@lsp.typemod.function.declaration"]    = { link = "@function" },
  ["@lsp.typemod.function.definition"]     = { link = "@function" },
  ["@lsp.typemod.method.declaration"]      = { link = "@function.method" },
  ["@lsp.typemod.method.definition"]       = { link = "@function.method" },
  ["@lsp.typemod.function.defaultLibrary"] = { link = "@function.builtin" },
  ["@lsp.typemod.method.defaultLibrary"]   = { link = "@function.builtin" },
  ["@lsp.typemod.variable.defaultLibrary"] = { link = "@variable.builtin" },
}

for group, spec in pairs(groups) do hl(group, spec) end

for i, c in ipairs(ansi) do vim.g["terminal_color_" .. (i - 1)] = c end
