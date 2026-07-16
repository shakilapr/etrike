import os

folder = r'c:\projects\etrike\control-toolkit'

replacements = {
    'Control Toolkit (Control Toolkit)': 'Control Toolkit',
    'architecture.md': 'architecture-control-toolkit.md',
    'architecture-control-toolkit.md]': 'architecture-control-toolkit.md]' # if we replaced [rchitecture.md] to [rchitecture-control-toolkit.md]
}

for filename in os.listdir(folder):
    if filename.endswith('.md'):
        filepath = os.path.join(folder, filename)
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
            
        content = content.replace('Control Toolkit (Control Toolkit)', 'Control Toolkit')
        content = content.replace('architecture.md', 'architecture-control-toolkit.md')
        
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
