<script lang="ts">
	import { Streamdown, type StreamdownProps } from 'svelte-streamdown';
	import { cn } from '$lib/utils';
	import { DEFAULT_ALLOWED_RESPONSE_IMAGE_PREFIXES } from '$lib/imagePolicy.js';

	type Props = StreamdownProps & {
		class?: string;
	};

	let { class: className, allowedImagePrefixes, ...restProps }: Props = $props();
</script>

<Streamdown
	class={cn('size-full [&>*:first-child]:mt-0 [&>*:last-child]:mb-0', className)}
	baseTheme="shadcn"
	static={true}
	parseIncompleteMarkdown={false}
	animation={{ enabled: false }}
	allowedImagePrefixes={allowedImagePrefixes ?? [...DEFAULT_ALLOWED_RESPONSE_IMAGE_PREFIXES]}
	{...restProps}
/>
