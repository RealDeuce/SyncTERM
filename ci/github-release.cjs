// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');

const tag = 'latest-build';
const retentionDays = 14;
const packages = [
  'syncterm-linux-x86_64.zip',
  'syncterm-windows-x86_64.zip',
  'syncterm-macos-arm64.zip',
  'syncterm-macos-x86_64.zip',
  'syncterm-macos-universal.zip',
];

async function optional(request) {
  try {
    return (await request()).data;
  } catch (error) {
    if (error.status === 404) return null;
    throw error;
  }
}

async function releaseFor(github, repo) {
  // The by-tag endpoint promises published releases only. Listing with the
  // write token also finds the draft left by an interrupted publication.
  const releases = await github.paginate(github.rest.repos.listReleases, {...repo, per_page: 100});
  const matches = releases.filter(release => release.tag_name === tag);
  if (matches.length > 1) throw new Error('Multiple latest-build releases require reconciliation');
  return matches[0] || null;
}

async function publish({github, context, core, directory = 'packages', now = new Date()}) {
  const repo = context.repo;
  const branch = context.payload.repository.default_branch;
  if (context.ref !== `refs/heads/${branch}`) throw new Error('Default branch required');
  const head = (await github.rest.git.getRef({...repo, ref: `heads/${branch}`})).data;
  if (head.object.sha !== context.sha) {
    core.info('Skipping a build superseded by a newer upstream push');
    return;
  }

  // Validate the complete local set before hiding or changing any public asset.
  const assets = [];
  for (const name of packages) {
    const data = await fs.readFile(path.join(directory, name));
    if (!data.length) throw new Error(`Empty package: ${name}`);
    assets.push({name, data});
  }
  const sums = assets.map(({name, data}) =>
    `${crypto.createHash('sha256').update(data).digest('hex')}  ${name}\n`).join('');
  assets.push({name: 'SHA256SUMS', data: Buffer.from(sums)});

  let release = await releaseFor(github, repo);
  if (release?.immutable) throw new Error('The latest-build prerelease must remain mutable');
  if (release && !release.prerelease) throw new Error('Refusing to replace a stable release');
  // Hide the old set while replacing it. A failed upload leaves a hidden draft,
  // rather than presenting packages from different commits as a complete build.
  if (release) {
    await github.rest.repos.updateRelease({...repo, release_id: release.id, draft: true});
  }
  const ref = await optional(() => github.rest.git.getRef({...repo, ref: `tags/${tag}`}));
  if (ref) {
    await github.rest.git.updateRef({...repo, ref: `tags/${tag}`, sha: context.sha, force: true});
  } else {
    await github.rest.git.createRef({...repo, ref: `refs/tags/${tag}`, sha: context.sha});
  }
  if (!release) {
    release = (await github.rest.repos.createRelease({
      ...repo, tag_name: tag, target_commitish: context.sha,
      name: 'Latest development build', draft: true, prerelease: true,
    })).data;
  }
  const previous = await github.paginate(github.rest.repos.listReleaseAssets,
    {...repo, release_id: release.id, per_page: 100});
  for (const asset of previous) {
    await github.rest.repos.deleteReleaseAsset({...repo, asset_id: asset.id});
  }
  for (const {name, data} of assets) {
    await github.rest.repos.uploadReleaseAsset({
      ...repo, release_id: release.id, name, data,
      headers: {'content-type': name.endsWith('.zip') ? 'application/zip' : 'text/plain'},
    });
  }
  const expires = new Date(now.getTime() + retentionDays * 86400000).toISOString();
  const run = `${context.serverUrl}/${repo.owner}/${repo.repo}/actions/runs/${context.runId}`;
  const body = [
    'Automated development builds; not a stable release.',
    `Commit: ${context.sha}`, `Built: ${now.toISOString()}`,
    `Expires after ${expires} (removed by the next daily cleanup).`,
    `[Workflow run](${run})`,
    ...packages.map(name => `[${name}](${context.serverUrl}/${repo.owner}/${repo.repo}/releases/download/${tag}/${name})`),
    'ZIP archives contain only the binaries, including the complete macOS app bundle. See the workflow reports for build configuration.',
    'These builds are unsigned. Optional runtime libraries may require separate installation.',
  ].join('\n\n');
  await github.rest.repos.updateRelease({
    ...repo, release_id: release.id, tag_name: tag,
    name: 'Latest development build', body, draft: false, prerelease: true,
    make_latest: 'false',
  });
  core.info(`Published ${tag} for ${context.sha}`);
}

async function expire({github, context, core, now = new Date()}) {
  const repo = context.repo;
  const release = await releaseFor(github, repo);
  if (!release) return;
  if (!release.prerelease || release.immutable) throw new Error('Expected a mutable prerelease');
  const assets = await github.paginate(github.rest.repos.listReleaseAssets,
    {...repo, release_id: release.id, per_page: 100});
  // Release creation time can refer to an old commit; uploads record the actual
  // build publication time even when the same release is reused for months.
  const times = assets.map(asset => Date.parse(asset.created_at));
  if (times.some(time => !Number.isFinite(time))) throw new Error('Invalid asset timestamp');
  if (times.length && now.getTime() - Math.max(...times) < retentionDays * 86400000) return;
  await github.rest.repos.deleteRelease({...repo, release_id: release.id});
  await optional(() => github.rest.git.deleteRef({...repo, ref: `tags/${tag}`}));
  core.info('Removed expired latest-build prerelease');
}

module.exports = {publish, expire};
