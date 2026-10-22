"""Shared, read-only reference access for the v2.3.0 behavior-preserving refactor."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = 'f7412ec90c925487617a1e8d40dd886c310142a6'
DRIVER = 'config/boards/arm/bbp9981/custom_driver/'
_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[A-Za-z_]\w*|(?:0[xX][0-9a-fA-F]+|\d+)[\w.]*|[^\s]', re.S)


def reference(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT)


def tokens(text):
    return [x for x in _TOKEN.findall(text) if not x.startswith(('/*', '//'))]


def function(text, name):
    """Extract one actual definition, not a declaration or a call."""
    # Mask comments without destroying string literals or line positions.
    masked = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',
                    lambda m: ' ' * len(m[0]) if m[0].startswith(('/',)) else m[0], text, flags=re.S)
    matches = list(re.finditer(r'(?m)^[ \t]*(?:(?:[A-Za-z_]\w*)[ \t*]+)+' +
                               re.escape(name) + r'\s*\([^;{}]*\)\s*\{', masked))
    assert len(matches) == 1, (name, len(matches))
    start = matches[0].start()
    opening = matches[0].end() - 1
    depth = 0
    # Braces in strings are tokens, not structure.
    for match in _TOKEN.finditer(text, opening):
        value = match[0]
        if value == '{': depth += 1
        elif value == '}':
            depth -= 1
            if depth == 0: return text[start:match.end()]
    raise AssertionError('Unclosed function: ' + name)


def expand_cmake(path):
    """Resolve only this board's literal local includes for contract comparison."""
    path = Path(path)
    from user_preferences_scope import verified_v232_view
    relative = str(path.resolve().relative_to(ROOT))
    text = verified_v232_view()[relative].decode()
    pattern = r'include\("\$\{CMAKE_CURRENT_LIST_DIR\}/([^"\n]+)"\)'
    return re.sub(pattern, lambda m: expand_cmake(path.parent / m[1]), text)
