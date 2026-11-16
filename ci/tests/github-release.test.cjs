// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {publish, expire} = require('../github-release.cjs');

const now = new Date('2026-11-16T12:00:00Z');
const names = ['syncterm-linux-x86_64.zip', 'syncterm-windows-x86_64.zip', 'syncterm-macos-arm64.zip'];
function fixture(release = null, assets = []) {
  if (release) release.tag_name = 'latest-build';
  const state = {release, assets, tag: release ? 'old' : null, mutations: [], failUpload: false};
  const missing = () => { throw Object.assign(new Error('Not found'), {status: 404}); };
  const change = (operation, fn) => async args => {
    state.mutations.push(operation);
    return {data: fn(args)};
  };
  const repos = {
    listReleases: async () => ({data: state.release ? [state.release] : []}),
    listReleaseAssets: async () => ({data: state.assets}),
    createRelease: change('create', args => (state.release = {id: 42, ...args})),
    updateRelease: change('update', args => Object.assign(state.release, args)),
    deleteRelease: change('delete', () => { state.release = null; }),
    deleteReleaseAsset: change('delete-asset', args => {
      state.assets = state.assets.filter(asset => asset.id !== args.asset_id);
    }),
    uploadReleaseAsset: change('upload', args => {
      assert.equal(state.release.draft, true, 'uploads must remain hidden');
      if (state.failUpload) throw new Error('Upload failed');
      const asset = {id: state.assets.length + 100, name: args.name, data: args.data,
        created_at: now.toISOString()};
      state.assets.push(asset);
      return asset;
    }),
  };
  const git = {
    getRef: async args => ({data: {object: {sha: args.ref.startsWith('heads/') ? state.head : state.tag || missing()}}}),
    createRef: change('create-tag', args => { state.tag = args.sha; }),
    updateRef: change('update-tag', args => { state.tag = args.sha; }),
    deleteRef: change('delete-tag', () => { if (!state.tag) missing(); state.tag = null; }),
  };
  state.head = 'new';
  return {state, github: {rest: {repos, git}, paginate: async (fn, args) => (await fn(args)).data},
    context: {repo: {owner: 'example', repo: 'mirror'}, sha: 'new', ref: 'refs/heads/master',
      payload: {repository: {default_branch: 'master'}}, serverUrl: 'https://github.com', runId: 123},
    core: {info() {}}, now};
}
async function withPackages(t, f) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'syncterm-release-'));
  t.after(() => fs.rm(directory, {recursive: true, force: true}));
  for (const name of names) await fs.writeFile(path.join(directory, name), `package ${name}`);
  return {...f, directory};
}

test('first successful matrix creates a prerelease with stable names and checksums', async t => {
  const f = await withPackages(t, fixture());
  await publish(f);
  assert.equal(f.state.tag, 'new');
  assert.equal(f.state.release.draft, false);
  assert.equal(f.state.release.prerelease, true);
  assert.equal(f.state.release.make_latest, 'false');
  assert.deepEqual(f.state.assets.map(asset => asset.name), [...names, 'SHA256SUMS']);
  assert.match(f.state.assets.at(-1).data.toString(), /^[a-f0-9]{64}  syncterm-linux-x86_64.zip\n/);
  assert.match(f.state.release.body, /2026-11-30T12:00:00.000Z/);
  assert.match(f.state.release.body, /releases\/download\/latest-build\/syncterm-windows-x86_64.zip/);
});

test('replacement removes old assets and recreates a tag removed by mirroring', async t => {
  const f = await withPackages(t, fixture({id: 42, prerelease: true}, [{id: 1, name: 'old.zip'}]));
  f.state.tag = null;
  await publish(f);
  assert.deepEqual(f.state.assets.map(asset => asset.name), [...names, 'SHA256SUMS']);
  assert.equal(f.state.tag, 'new');
  assert.equal(f.state.mutations[0], 'update');
  assert.equal(f.state.mutations.at(-1), 'update');
});

test('missing platform package preserves the previous public build', async t => {
  const f = await withPackages(t, fixture({id: 42, prerelease: true}));
  await fs.unlink(path.join(f.directory, names[1]));
  await assert.rejects(publish(f), /ENOENT/);
  assert.deepEqual(f.state.mutations, []);
});

test('superseded builds cannot overwrite current packages', async () => {
  const f = fixture();
  f.state.head = 'newer';
  await publish(f);
  assert.deepEqual(f.state.mutations, []);
});

test('failed upload stays hidden and the next run recovers', async t => {
  const f = await withPackages(t, fixture({id: 42, prerelease: true}));
  f.state.failUpload = true;
  await assert.rejects(publish(f), /Upload failed/);
  assert.equal(f.state.release.draft, true);
  f.state.failUpload = false;
  await publish(f);
  assert.equal(f.state.release.draft, false);
  assert.equal(f.state.assets.length, 4);
});

test('authorization failures are not mistaken for missing releases', async t => {
  const f = await withPackages(t, fixture());
  f.github.rest.repos.listReleases = async () => { throw Object.assign(new Error('Forbidden'), {status: 403}); };
  await assert.rejects(publish(f), /Forbidden/);
  assert.deepEqual(f.state.mutations, []);
});

test('cleanup uses upload time when the release itself is old', async () => {
  const f = fixture({id: 42, prerelease: true, created_at: '2020-01-01T00:00:00Z'},
    [{id: 1, created_at: '2026-11-15T12:00:00Z'}]);
  await expire(f);
  assert.deepEqual(f.state.mutations, []);
});

test('cleanup expires packages at 14 days and tolerates a missing mirror tag', async () => {
  const f = fixture({id: 42, prerelease: true}, [{id: 1, created_at: '2026-11-02T12:00:00Z'}]);
  f.state.tag = null;
  await expire(f);
  assert.equal(f.state.release, null);
  assert.deepEqual(f.state.mutations, ['delete', 'delete-tag']);
});

test('cleanup handles an absent release and empty failed draft', async () => {
  const absent = fixture();
  await expire(absent);
  assert.deepEqual(absent.state.mutations, []);
  const draft = fixture({id: 42, prerelease: true, draft: true});
  await expire(draft);
  assert.equal(draft.state.release, null);
});

test('cleanup refuses to touch a stable release or malformed timestamps', async () => {
  const stable = fixture({id: 42, prerelease: false});
  await assert.rejects(expire(stable), /mutable prerelease/);
  assert.deepEqual(stable.state.mutations, []);
  const invalid = fixture({id: 42, prerelease: true}, [{created_at: 'invalid'}]);
  await assert.rejects(expire(invalid), /Invalid asset timestamp/);
  assert.deepEqual(invalid.state.mutations, []);
});
