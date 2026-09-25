// 在 PR 上发一条「sticky」CI 报告评论：按 marker 找到已有评论就原地更新，不会刷屏。
// 用法（workflow 里）：
//   await commentVisualReport({ github, context, artifactName, imageBaseUrl, buildResult })
//
// 图片 URL：报告里的 ![x](foo.png) 会被重写成 ${imageBaseUrl}/foo.png
// （imageBaseUrl 指向 ci-assets 分支的 runs/<run_id> 目录）

const fs = require('fs');
const path = require('path');

const COMMENT_MARKER = '<!-- spacewm-ci-report -->';

/** 把 markdown 里的相对图片路径重写成绝对 URL。 */
function rewriteImageLinks(markdown, imageBaseUrl) {
  if (!imageBaseUrl) return markdown;
  const base = imageBaseUrl.replace(/\/$/, '');
  return markdown.replace(/!\[([^\]]*)\]\((?!https?:)([^)]+)\)/g, (m, alt, src) => {
    const file = path.basename(src.trim());
    return `![${alt}](${base}/${file})`;
  });
}

/** 读取已下载的报告 artifact。 */
function readReport(reportDir) {
  const candidates = [
    path.join(reportDir, 'report.md'),
    path.join(reportDir, 'visual-report', 'report.md'),
    path.join(reportDir, 'report', 'report.md'),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return fs.readFileSync(c, 'utf8');
  }
  return '';
}

async function commentVisualReport({
  github,
  context,
  artifactName = 'visual-report',
  imageBaseUrl = process.env.IMAGE_BASE_URL || '',
  buildResult = '',
  reportDir = process.env.GITHUB_WORKSPACE || '.',
}) {
  const { owner, repo } = context.repo;
  const issue_number = context.issue.number;

  let body = readReport(reportDir);
  if (!body) {
    body = `## ${buildResult === 'failure' ? '❌' : '⚠️'} SpaceWM CI\n\n报告产物缺失（artifact: ${artifactName}）。请查看 workflow 日志。`;
  }
  if (buildResult && buildResult !== 'success') {
    body = `> Build & Test 结果为 \`${buildResult}\`，截图/单测结论可能不完整。\n\n` + body;
  }
  body = rewriteImageLinks(body, imageBaseUrl);
  body = `${COMMENT_MARKER}\n${body}\n\n<sub>由 CI 自动生成 · run ${context.runId}</sub>`;

  const comments = await github.paginate(github.rest.issues.listComments, {
    owner,
    repo,
    issue_number,
    per_page: 100,
  });
  const existing = comments.find((c) => typeof c.body === 'string' && c.body.includes(COMMENT_MARKER));

  if (existing) {
    await github.rest.issues.updateComment({ owner, repo, comment_id: existing.id, body });
    return { action: 'updated', comment_id: existing.id };
  }
  const created = await github.rest.issues.createComment({ owner, repo, issue_number, body });
  return { action: 'created', comment_id: created.data.id };
}

module.exports = { commentVisualReport, rewriteImageLinks, COMMENT_MARKER };
