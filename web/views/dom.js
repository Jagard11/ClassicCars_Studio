export function el(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    if (tag === 'button' && !props.type) node.type = 'button';
    for (const [key, value] of Object.entries(props)) {
        if (value == null || value === false) continue;
        if (key === 'class') node.className = value;
        else if (key === 'text') node.textContent = String(value);
        else if (value === true) node.setAttribute(key, '');
        else node.setAttribute(key, String(value));
    }
    for (const child of [].concat(children)) {
        if (child == null || child === false) continue;
        node.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return node;
}
