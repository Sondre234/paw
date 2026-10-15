-- mochi (餅) for Neovim: Catppuccin Mocha on glass. The editor paints no background of its own,
-- so kitty's translucency (and paw's blur behind it) shows through the text; floats, menus, the
-- status line and the cursor line keep a tint of Mocha's mantle and surface, which mochi's kitty
-- theme renders as glass too. Colours from Catppuccin Mocha, MIT
-- (https://github.com/catppuccin/catppuccin); the scheme is paw's, GPL-3.0-or-later.
--
-- Plugin-free. Copy this file to ~/.config/nvim/colors/ (or put the editors/nvim directory on
-- 'runtimepath') and run `:colorscheme mochi`. True colour with 'termguicolors' (Neovim's default
-- in kitty); without it, the ANSI indices match the mochi kitty palette. In a terminal without
-- translucency the page is the terminal's own background: set it to Mocha's base, #1E1E2E.
--
-- Catppuccin's style: keywords in mauve, strings in green, functions in blue, types in yellow,
-- numbers and constants in peach, operators in sky, parameters in maroon, members in lavender,
-- comments in overlay italic; the cursor and the selection in pink, links and focus in blue.

-- Each colour is { truecolor, ANSI index }. An index of nil leaves the terminal's own colour.
local p = {
  -- Mocha
  rosewater = { "#F5E0DC", 15 },
  flamingo  = { "#F2CDCD", 15 },
  pink      = { "#F5C2E7", 5 },
  mauve     = { "#CBA6F7", 13 },
  red       = { "#F38BA8", 1 },
  maroon    = { "#EBA0AC", 9 },
  peach     = { "#FAB387", 11 },
  yellow    = { "#F9E2AF", 3 },
  green     = { "#A6E3A1", 2 },
  teal      = { "#94E2D5", 6 },
  sky       = { "#89DCEB", 14 },
  sapphire  = { "#74C7EC", 12 },
  blue      = { "#89B4FA", 4 },
  lavender  = { "#B4BEFE", 12 },
  text      = { "#CDD6F4", 15 },
  subtext1  = { "#BAC2DE", 7 },
  subtext0  = { "#A6ADC8", 7 },
  overlay2  = { "#9399B2", 7 },
  overlay1  = { "#7F849C", 8 },
  overlay0  = { "#6C7086", 8 },
  surface2  = { "#585B70", 8 },
  surface1  = { "#45475A", 0 },
  surface0  = { "#313244", 0 },
  base      = { "#1E1E2E", 0 },
  mantle    = { "#181825", 0 },
  crust     = { "#11111B", 0 },

  -- Blends of them: Catppuccin's cursor line, mochi's pink selection, and washes of colour
  -- behind diffs and diagnostics (each the accent over base).
  line      = { "#2A2B3C" },        -- the cursor line: surface0 at 64% over base
  sel       = { "#4D4257", 0 },     -- the selection: pink at 22% over base, as in kitty
  search    = { "#3E5767", 4 },     -- search hits: sky at 30% over base
  add_bg    = { "#364143" },        -- green 18%
  del_bg    = { "#443244" },        -- red 18%
  change_bg = { "#25283C" },        -- blue 7%
  text_bg   = { "#3E4B6B" },        -- changed text in a changed line: blue 30%
  red_bg    = { "#32283A" },        -- red 9.5%, behind virtual text
  yellow_bg = { "#33313A" },
  sky_bg    = { "#283040" },
  teal_bg   = { "#29313E" },
  green_bg  = { "#2B3139" },

  shadow_blend = 60, -- how much a float's shadow lets through, 0-100
}

local ansi = {
  "#45475A", "#F38BA8", "#A6E3A1", "#F9E2AF", "#89B4FA", "#F5C2E7", "#94E2D5", "#BAC2DE",
  "#585B70", "#F37799", "#89D88B", "#EBD391", "#74A8FC", "#F2AEDE", "#6BD7CA", "#A6ADC8",
}

vim.cmd("highlight clear")
if vim.fn.exists("syntax_on") == 1 then vim.cmd("syntax reset") end
vim.o.background = "dark"
vim.g.colors_name = "mochi"

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
  FloatBorder      = { fg = p.mauve, bg = p.mantle },
  FloatTitle       = { fg = p.pink, bg = p.mantle, bold = true },
  FloatFooter      = { fg = p.overlay1, bg = p.mantle },
  FloatShadow      = { bg = p.crust, blend = p.shadow_blend },
  FloatShadowThrough = { bg = p.crust, blend = 100 },
  Cursor           = { fg = p.base, bg = p.pink },
  lCursor          = { link = "Cursor" },
  CursorIM         = { link = "Cursor" },
  TermCursor       = { fg = p.base, bg = p.pink },
  CursorLine       = { bg = p.line },
  CursorColumn     = { bg = p.line },
  ColorColumn      = { bg = p.line },
  LineNr           = { fg = p.surface2 },
  LineNrAbove      = { link = "LineNr" },
  LineNrBelow      = { link = "LineNr" },
  CursorLineNr     = { fg = p.lavender, bold = true },
  CursorLineFold   = { fg = p.lavender },
  CursorLineSign   = {},
  SignColumn       = { fg = p.surface1 },
  FoldColumn       = { fg = p.overlay0 },
  Folded           = { fg = p.blue, bg = p.surface0, italic = true },
  WinSeparator     = { fg = p.surface1 },
  VertSplit        = { link = "WinSeparator" },
  StatusLine       = { fg = p.text, bg = p.mantle },
  StatusLineNC     = { fg = p.overlay0, bg = p.mantle },
  StatusLineTerm   = { link = "StatusLine" },
  StatusLineTermNC = { link = "StatusLineNC" },
  WinBar           = { fg = p.rosewater, bold = true },
  WinBarNC         = { fg = p.overlay1 },
  TabLine          = { fg = p.overlay1, bg = p.mantle },
  TabLineSel       = { fg = p.mauve, bg = p.surface0, bold = true },
  TabLineFill      = {},
  Pmenu            = { fg = p.overlay2, bg = p.mantle },
  PmenuSel         = { fg = p.text, bg = p.surface0, bold = true },
  PmenuKind        = { fg = p.lavender, bg = p.mantle },
  PmenuKindSel     = { fg = p.lavender, bg = p.surface0, bold = true },
  PmenuExtra       = { fg = p.overlay0, bg = p.mantle },
  PmenuExtraSel    = { fg = p.overlay2, bg = p.surface0 },
  PmenuMatch       = { fg = p.pink, bg = p.mantle, bold = true },
  PmenuMatchSel    = { fg = p.pink, bg = p.surface0, bold = true },
  PmenuSbar        = { bg = p.surface0 },
  PmenuThumb       = { bg = p.overlay0 },
  ComplMatchIns    = { fg = p.overlay0 },
  WildMenu         = { link = "PmenuSel" },
  Visual           = { bg = p.sel, bold = true, cterm = { reverse = true } },
  VisualNOS        = { link = "Visual" },
  VisualNC         = { link = "Visual" },
  Search           = { fg = p.text, bg = p.search },
  CurSearch        = { fg = p.mantle, bg = p.pink, bold = true },
  IncSearch        = { link = "CurSearch" },
  Substitute       = { fg = p.mantle, bg = p.red },
  MatchParen       = { fg = p.peach, bg = p.surface1, bold = true, cterm = { bold = true, underline = true } },
  QuickFixLine     = { bg = p.surface1, bold = true },
  NonText          = { fg = p.overlay0 },
  EndOfBuffer      = { fg = p.surface0 },
  Whitespace       = { fg = p.surface1 },
  SpecialKey       = { fg = p.surface2 },
  Conceal          = { fg = p.overlay1 },
  Directory        = { fg = p.blue },
  Title            = { fg = p.blue, bold = true },
  Question         = { fg = p.blue },
  MoreMsg          = { fg = p.blue },
  ModeMsg          = { fg = p.text, bold = true },
  MsgArea          = {},
  MsgSeparator     = { fg = p.surface1 },
  WarningMsg       = { fg = p.yellow },
  ErrorMsg         = { fg = p.red, bold = true, italic = true },
  SpellBad         = { undercurl = true, sp = p.red },
  SpellCap         = { undercurl = true, sp = p.yellow },
  SpellLocal       = { undercurl = true, sp = p.blue },
  SpellRare        = { undercurl = true, sp = p.green },
  DiffAdd          = { bg = p.add_bg, ctermfg = p.green[2] },
  DiffChange       = { bg = p.change_bg, ctermfg = p.blue[2] },
  DiffDelete       = { fg = p.red, bg = p.del_bg },
  DiffText         = { bg = p.text_bg, ctermfg = p.blue[2] },
  Added            = { fg = p.green },
  Changed          = { fg = p.blue },
  Removed          = { fg = p.red },
  SnippetTabstop   = { bg = p.surface0 },
  RedrawDebugNormal = { reverse = true },

  -- Diagnostics -------------------------------------------------------------------------------
  DiagnosticError            = { fg = p.red },
  DiagnosticWarn             = { fg = p.yellow },
  DiagnosticInfo             = { fg = p.sky },
  DiagnosticHint             = { fg = p.teal },
  DiagnosticOk               = { fg = p.green },
  DiagnosticVirtualTextError = { fg = p.red, bg = p.red_bg, italic = true },
  DiagnosticVirtualTextWarn  = { fg = p.yellow, bg = p.yellow_bg, italic = true },
  DiagnosticVirtualTextInfo  = { fg = p.sky, bg = p.sky_bg, italic = true },
  DiagnosticVirtualTextHint  = { fg = p.teal, bg = p.teal_bg, italic = true },
  DiagnosticVirtualTextOk    = { fg = p.green, bg = p.green_bg, italic = true },
  DiagnosticVirtualLinesError = { fg = p.red },
  DiagnosticVirtualLinesWarn = { fg = p.yellow },
  DiagnosticVirtualLinesInfo = { fg = p.sky },
  DiagnosticVirtualLinesHint = { fg = p.teal },
  DiagnosticVirtualLinesOk   = { fg = p.green },
  DiagnosticUnderlineError   = { undercurl = true, sp = p.red },
  DiagnosticUnderlineWarn    = { undercurl = true, sp = p.yellow },
  DiagnosticUnderlineInfo    = { undercurl = true, sp = p.sky },
  DiagnosticUnderlineHint    = { undercurl = true, sp = p.teal },
  DiagnosticUnderlineOk      = { undercurl = true, sp = p.green },
  DiagnosticSignError        = { fg = p.red },
  DiagnosticSignWarn         = { fg = p.yellow },
  DiagnosticSignInfo         = { fg = p.sky },
  DiagnosticSignHint         = { fg = p.teal },
  DiagnosticSignOk           = { fg = p.green },
  DiagnosticFloatingError    = { fg = p.red },
  DiagnosticFloatingWarn     = { fg = p.yellow },
  DiagnosticFloatingInfo     = { fg = p.sky },
  DiagnosticFloatingHint     = { fg = p.teal },
  DiagnosticFloatingOk       = { fg = p.green },
  DiagnosticUnnecessary      = { fg = p.overlay0 },
  DiagnosticDeprecated       = { strikethrough = true, sp = p.overlay1 },

  -- LSP ---------------------------------------------------------------------------------------
  LspReferenceText            = { bg = p.surface1 },
  LspReferenceRead            = { bg = p.surface1 },
  LspReferenceWrite           = { bg = p.surface1, underline = true, sp = p.pink },
  LspReferenceTarget          = { link = "LspReferenceText" },
  LspInlayHint                = { fg = p.overlay0, italic = true },
  LspCodeLens                 = { fg = p.overlay0, italic = true },
  LspCodeLensSeparator        = { fg = p.surface1 },
  LspSignatureActiveParameter = { fg = p.peach, bold = true, underline = true },

  -- Syntax (legacy groups, still used by every non-Tree-sitter filetype) ------------------------
  Comment        = { fg = p.overlay2, italic = true },
  Constant       = { fg = p.peach },
  String         = { fg = p.green },
  Character      = { fg = p.teal },
  Number         = { fg = p.peach },
  Boolean        = { fg = p.peach },
  Float          = { fg = p.peach },
  Identifier     = { fg = p.flamingo },
  Function       = { fg = p.blue },
  Statement      = { fg = p.mauve },
  Conditional    = { fg = p.mauve },
  Repeat         = { fg = p.mauve },
  Label          = { fg = p.sapphire },
  Operator       = { fg = p.sky },
  Keyword        = { fg = p.mauve },
  Exception      = { fg = p.mauve },
  PreProc        = { fg = p.pink },
  Include        = { fg = p.mauve },
  Define         = { fg = p.pink },
  Macro          = { fg = p.mauve },
  PreCondit      = { fg = p.pink },
  Type           = { fg = p.yellow },
  StorageClass   = { fg = p.yellow },
  Structure      = { fg = p.yellow },
  Typedef        = { fg = p.yellow },
  Special        = { fg = p.pink },
  SpecialChar    = { fg = p.pink },
  Tag            = { fg = p.lavender },
  Delimiter      = { fg = p.overlay2 },
  SpecialComment = { fg = p.overlay2, bold = true, italic = true },
  Debug          = { fg = p.pink },
  Underlined     = { fg = p.blue, underline = true },
  Ignore         = { fg = p.overlay0 },
  Error          = { fg = p.red, bold = true },
  Todo           = { fg = p.base, bg = p.flamingo, bold = true },

  -- Tree-sitter -------------------------------------------------------------------------------
  ["@variable"]                   = { fg = p.text },
  ["@variable.builtin"]           = { fg = p.red },
  ["@variable.parameter"]         = { fg = p.maroon },
  ["@variable.parameter.builtin"] = { fg = p.maroon, italic = true },
  ["@variable.member"]            = { fg = p.lavender },
  ["@property"]                   = { fg = p.lavender },

  ["@constant"]         = { fg = p.peach },
  ["@constant.builtin"] = { fg = p.peach },
  ["@constant.macro"]   = { fg = p.mauve },

  ["@module"]         = { fg = p.lavender, italic = true },
  ["@module.builtin"] = { fg = p.lavender, italic = true },
  ["@label"]          = { fg = p.sapphire },

  ["@string"]               = { fg = p.green },
  ["@string.documentation"] = { fg = p.teal },
  ["@string.regexp"]        = { fg = p.pink },
  ["@string.escape"]        = { fg = p.pink },
  ["@string.special"]       = { fg = p.pink },
  ["@string.special.symbol"] = { fg = p.flamingo },
  ["@string.special.path"]  = { fg = p.pink },
  ["@string.special.url"]   = { fg = p.rosewater, italic = true, underline = true },
  ["@character"]            = { fg = p.teal },
  ["@character.special"]    = { fg = p.pink },

  ["@boolean"]      = { fg = p.peach },
  ["@number"]       = { fg = p.peach },
  ["@number.float"] = { fg = p.peach },

  ["@type"]            = { fg = p.yellow },
  ["@type.builtin"]    = { fg = p.yellow, italic = true },
  ["@type.definition"] = { fg = p.yellow },

  ["@attribute"]         = { fg = p.yellow },
  ["@attribute.builtin"] = { fg = p.yellow },

  ["@function"]             = { fg = p.blue },
  ["@function.builtin"]     = { fg = p.peach },
  ["@function.call"]        = { fg = p.blue },
  ["@function.macro"]       = { fg = p.teal },
  ["@function.method"]      = { fg = p.blue },
  ["@function.method.call"] = { fg = p.blue },
  ["@constructor"]          = { fg = p.sapphire },
  ["@constructor.lua"]      = { fg = p.flamingo }, -- a table's braces
  ["@operator"]             = { fg = p.sky },

  ["@keyword"]                     = { fg = p.mauve },
  ["@keyword.coroutine"]           = { fg = p.mauve },
  ["@keyword.function"]            = { fg = p.mauve },
  ["@keyword.operator"]            = { fg = p.mauve },
  ["@keyword.import"]              = { fg = p.mauve },
  ["@keyword.type"]                = { fg = p.mauve },
  ["@keyword.modifier"]            = { fg = p.mauve },
  ["@keyword.repeat"]              = { fg = p.mauve },
  ["@keyword.return"]              = { fg = p.mauve },
  ["@keyword.debug"]               = { fg = p.pink },
  ["@keyword.exception"]           = { fg = p.mauve },
  ["@keyword.conditional"]         = { fg = p.mauve },
  ["@keyword.conditional.ternary"] = { fg = p.mauve },
  ["@keyword.directive"]           = { fg = p.pink },
  ["@keyword.directive.define"]    = { fg = p.pink },

  ["@punctuation.delimiter"] = { fg = p.overlay2 },
  ["@punctuation.bracket"]   = { fg = p.overlay2 },
  ["@punctuation.special"]   = { fg = p.sky },

  ["@comment"]               = { link = "Comment" },
  ["@comment.documentation"] = { link = "Comment" },
  ["@comment.error"]         = { fg = p.base, bg = p.red, bold = true },
  ["@comment.warning"]       = { fg = p.base, bg = p.yellow, bold = true },
  ["@comment.todo"]          = { fg = p.base, bg = p.flamingo, bold = true },
  ["@comment.note"]          = { fg = p.base, bg = p.rosewater, bold = true },

  ["@markup.strong"]           = { fg = p.red, bold = true },
  ["@markup.italic"]           = { fg = p.red, italic = true },
  ["@markup.strikethrough"]    = { fg = p.text, strikethrough = true },
  ["@markup.underline"]        = { underline = true },
  ["@markup.heading"]          = { fg = p.blue, bold = true },
  ["@markup.heading.1"]        = { fg = p.red, bold = true },
  ["@markup.heading.2"]        = { fg = p.peach, bold = true },
  ["@markup.heading.3"]        = { fg = p.yellow, bold = true },
  ["@markup.heading.4"]        = { fg = p.green, bold = true },
  ["@markup.heading.5"]        = { fg = p.sapphire, bold = true },
  ["@markup.heading.6"]        = { fg = p.lavender, bold = true },
  ["@markup.quote"]            = { fg = p.maroon, italic = true },
  ["@markup.math"]             = { fg = p.blue },
  ["@markup.link"]             = { fg = p.lavender },
  ["@markup.link.label"]       = { fg = p.lavender },
  ["@markup.link.url"]         = { fg = p.rosewater, italic = true, underline = true },
  ["@markup.raw"]              = { fg = p.teal },
  ["@markup.raw.block"]        = { fg = p.subtext1 },
  ["@markup.list"]             = { fg = p.teal },
  ["@markup.list.checked"]     = { fg = p.green },
  ["@markup.list.unchecked"]   = { fg = p.overlay1 },
  ["@markup.environment"]      = { fg = p.pink },

  ["@diff.plus"]  = { link = "Added" },
  ["@diff.minus"] = { link = "Removed" },
  ["@diff.delta"] = { link = "Changed" },

  ["@tag"]           = { fg = p.mauve },
  ["@tag.builtin"]   = { fg = p.mauve },
  ["@tag.attribute"] = { fg = p.teal, italic = true },
  ["@tag.delimiter"] = { fg = p.sky },

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
