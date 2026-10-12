---
title: Highlighting
tags: [fixture, markdown]
---
# Heading one
## Heading two `with code` and [[Welcome]]
###### Heading six

Text with **bold**, __bold__, *italic*, _italic_ and ***both***.
Not emphasis: snake_case_name, 2 * 3 * 4, and a lone *.

Inline `code`, ``code with a ` backtick``, and an `unclosed backtick.
Links: [[Welcome]], [[Folder/Second note|alias]], [[Welcome#Heading]], ![[image.png]] and [[unclosed.

```js
// # not a heading, **not bold**
const a = `template`;
```

````
```
still code: a shorter fence does not close it
````

~~~
Tildes do not open a code block.

---
A --- after the first line is not frontmatter.
