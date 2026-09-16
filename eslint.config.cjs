const js = require('@eslint/js')
const react = require('eslint-plugin-react')
const { configs } = require('@electron-toolkit/eslint-config-ts')

module.exports = [
  {
    ignores: ['**/node_modules/**', '**/dist/**', '**/out/**', '**/extra/**', 'cpp/build/**']
  },

  js.configs.recommended,
  ...configs.recommended,

  {
    rules: {
      'preserve-caught-error': 'off',
      'no-constant-binary-expression': 'error',
      'no-constructor-return': 'error',
      'no-promise-executor-return': 'error',
      'no-self-compare': 'error',
      'no-template-curly-in-string': 'error',
      'no-unmodified-loop-condition': 'error',
      'no-unreachable-loop': 'error',
      'no-useless-assignment': 'error'
    }
  },

  {
    files: ['src/renderer/src/**/*.{jsx,tsx}'],
    plugins: {
      react
    },
    rules: {
      ...react.configs.recommended.rules,
      ...react.configs['jsx-runtime'].rules
    },
    settings: {
      react: {
        version: '19.2.4'
      }
    },
    languageOptions: {
      ...react.configs.recommended.languageOptions
    }
  },

  {
    files: ['**/*.cjs', '**/*.mjs'],
    rules: {
      '@typescript-eslint/no-require-imports': 'off'
    }
  },

  {
    // Scripts executed inside the QuickJS and MITM sandboxes. The host injects
    // their globals, and entry points such as onStartProxy or onRequest are
    // invoked by name from C++, so they look undefined or unused to lint.
    files: ['scripts/**/*.js', 'cpp/scripts/**/*.js', 'plugins/**/*.js', 'mitm/**/*.js'],
    languageOptions: {
      globals: {
        core: 'readonly',
        ui: 'readonly',
        sparkle: 'readonly',
        yaml: 'readonly',
        b64e: 'readonly',
        b64d: 'readonly'
      }
    },
    rules: {
      '@typescript-eslint/no-unused-vars': 'off',
      '@typescript-eslint/explicit-function-return-type': 'off'
    }
  },

  {
    files: ['**/*.{ts,tsx}'],
    rules: {
      '@typescript-eslint/no-unused-vars': [
        'error',
        {
          argsIgnorePattern: '^_',
          caughtErrors: 'none',
          destructuredArrayIgnorePattern: '^_',
          ignoreRestSiblings: true,
          varsIgnorePattern: '^_'
        }
      ],
      '@typescript-eslint/explicit-function-return-type': 'off',
      '@typescript-eslint/no-explicit-any': 'error'
    }
  }
]
